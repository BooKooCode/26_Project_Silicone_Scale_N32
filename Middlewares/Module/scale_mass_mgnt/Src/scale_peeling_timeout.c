#include "scale_peeling_timeout.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SCALE_PEELING_MAD_EPSILON_G       0.000001f
#define SCALE_PEELING_ZERO_MAD_LIMIT_G    0.2f
#define SCALE_PEELING_MAD_SCALE           1.4826f
#define SCALE_PEELING_MAD_MULTIPLIER      3.0f
#define SCALE_PEELING_TREND_MIN_G         0.2f
#define SCALE_PEELING_TREND_RESIDUAL_GAIN 3.0f
#define SCALE_PEELING_TRIM_COUNT          2U
#define SCALE_PEELING_MIN_ESTIMATE_COUNT  5U

static void sort_float(float *values, uint32_t count)
{
    for(uint32_t index = 1U; index < count; index++) {
        float value = values[index];
        uint32_t insert_at = index;
        while((insert_at > 0U) && (values[insert_at - 1U] > value)) {
            values[insert_at] = values[insert_at - 1U];
            insert_at--;
        }
        values[insert_at] = value;
    }
}

static float median_of_sorted(const float *values, uint32_t count)
{
    uint32_t middle = count / 2U;
    if((count & 1U) != 0U) {
        return values[middle];
    }
    return (values[middle - 1U] + values[middle]) * 0.5f;
}

static float clamp_float(float value, float lower, float upper)
{
    if(value < lower) {
        return lower;
    }
    if(value > upper) {
        return upper;
    }
    return value;
}

bool scale_peeling_estimate(const float *samples,
                            uint32_t count,
                            scale_peeling_estimate_t *result)
{
    float valid[SCALE_PEELING_SAMPLE_COUNT];
    float work[SCALE_PEELING_SAMPLE_COUNT];
    float latest = 0.0f;
    uint32_t valid_count = 0U;

    if((samples == NULL) || (result == NULL) || (count == 0U) ||
       (count > SCALE_PEELING_SAMPLE_COUNT)) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    for(uint32_t index = 0U; index < count; index++) {
        if(isfinite(samples[index])) {
            valid[valid_count++] = samples[index];
            latest = samples[index];
        }
    }
    if(valid_count == 0U) {
        return false;
    }

    result->value = latest;
    result->mode = SCALE_PEELING_ESTIMATE_LATEST;
    if(valid_count < SCALE_PEELING_MIN_ESTIMATE_COUNT) {
        return true;
    }

    memcpy(work, valid, valid_count * sizeof(float));
    sort_float(work, valid_count);
    float median = median_of_sorted(work, valid_count);

    for(uint32_t index = 0U; index < valid_count; index++) {
        work[index] = fabsf(valid[index] - median);
    }
    sort_float(work, valid_count);
    float mad = median_of_sorted(work, valid_count);
    float limit = SCALE_PEELING_ZERO_MAD_LIMIT_G;
    if(mad > SCALE_PEELING_MAD_EPSILON_G) {
        limit = SCALE_PEELING_MAD_MULTIPLIER * SCALE_PEELING_MAD_SCALE * mad;
    }
    float lower = median - limit;
    float upper = median + limit;
    for(uint32_t index = 0U; index < valid_count; index++) {
        work[index] = clamp_float(valid[index], lower, upper);
    }

    float mean_x = ((float)valid_count - 1.0f) * 0.5f;
    float mean_y = 0.0f;
    for(uint32_t index = 0U; index < valid_count; index++) {
        mean_y += work[index];
    }
    mean_y /= (float)valid_count;

    float covariance = 0.0f;
    float variance_x = 0.0f;
    for(uint32_t index = 0U; index < valid_count; index++) {
        float centered_x = (float)index - mean_x;
        covariance += centered_x * (work[index] - mean_y);
        variance_x += centered_x * centered_x;
    }
    float slope = covariance / variance_x;
    float intercept = mean_y - slope * mean_x;

    float residual_sum = 0.0f;
    for(uint32_t index = 0U; index < valid_count; index++) {
        float error = work[index] - (intercept + slope * (float)index);
        residual_sum += error * error;
    }
    float residual = sqrtf(residual_sum / (float)valid_count);
    float trend_delta = fabsf(slope) * ((float)valid_count - 1.0f);
    if(!isfinite(residual) || !isfinite(trend_delta)) {
        return true;
    }

    result->residual = residual;
    result->trend_delta = trend_delta;
    if(trend_delta > fmaxf(SCALE_PEELING_TREND_MIN_G,
                           SCALE_PEELING_TREND_RESIDUAL_GAIN * residual)) {
        float endpoint = intercept + slope * ((float)valid_count - 1.0f);
        if(isfinite(endpoint)) {
            result->value = endpoint;
            result->mode = SCALE_PEELING_ESTIMATE_TREND_ENDPOINT;
        }
        return true;
    }

    sort_float(work, valid_count);
    float trimmed_sum = 0.0f;
    for(uint32_t index = SCALE_PEELING_TRIM_COUNT;
        index < (valid_count - SCALE_PEELING_TRIM_COUNT);
        index++) {
        trimmed_sum += work[index];
    }
    float trimmed_mean = trimmed_sum /
        (float)(valid_count - (2U * SCALE_PEELING_TRIM_COUNT));
    if(isfinite(trimmed_mean)) {
        result->value = trimmed_mean;
        result->mode = SCALE_PEELING_ESTIMATE_TRIMMED_MEAN;
    }
    return true;
}

void scale_peeling_session_init(scale_peeling_session_t *session)
{
    if(session != NULL) {
        memset(session, 0, sizeof(*session));
    }
}

bool scale_peeling_session_start(scale_peeling_session_t *session, float original_tare)
{
    if((session == NULL) || session->active) {
        return false;
    }
    scale_peeling_session_init(session);
    session->original_tare = original_tare;
    session->active = true;
    return true;
}

void scale_peeling_session_push(scale_peeling_session_t *session, float sample)
{
    if((session == NULL) || !session->active) {
        return;
    }

    session->elapsed_cycles++;
    if(!isfinite(sample)) {
        return;
    }

    session->samples[session->write_index] = sample;
    session->write_index = (session->write_index + 1U) % SCALE_PEELING_SAMPLE_COUNT;
    if(session->sample_count < SCALE_PEELING_SAMPLE_COUNT) {
        session->sample_count++;
    }
}

bool scale_peeling_session_timed_out(const scale_peeling_session_t *session)
{
    return (session != NULL) && session->active &&
           (session->elapsed_cycles >= SCALE_PEELING_TIMEOUT_CYCLES);
}

bool scale_peeling_session_estimate(const scale_peeling_session_t *session,
                                    scale_peeling_estimate_t *result)
{
    float ordered[SCALE_PEELING_SAMPLE_COUNT];

    if((session == NULL) || !session->active ||
       (session->sample_count == 0U) || (result == NULL)) {
        return false;
    }

    uint32_t oldest = (session->write_index + SCALE_PEELING_SAMPLE_COUNT -
                       session->sample_count) % SCALE_PEELING_SAMPLE_COUNT;
    for(uint32_t index = 0U; index < session->sample_count; index++) {
        ordered[index] = session->samples[
            (oldest + index) % SCALE_PEELING_SAMPLE_COUNT];
    }
    return scale_peeling_estimate(ordered, session->sample_count, result);
}

float scale_peeling_session_cancel(scale_peeling_session_t *session)
{
    if(session == NULL) {
        return 0.0f;
    }
    float original_tare = session->original_tare;
    scale_peeling_session_init(session);
    return original_tare;
}

void scale_peeling_session_complete(scale_peeling_session_t *session)
{
    scale_peeling_session_init(session);
}
