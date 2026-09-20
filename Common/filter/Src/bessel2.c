#include <stdio.h>
#include "math.h"
#include "bessel2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif


#define BESSEL_Q 1.3617f        // 二阶贝塞尔滤波器的品质因数Q
#define BESSEL_ALPHA 1.7321f    // 阻尼系数




void bessel2_init(BesselFilter2nd* filter, float fs, float fc) {
    if(NULL == filter || fs <= 0 || fc <= 0 || fc >= fs / 2) {
        return;
    }
    
    filter->fs = fs;
    filter->fc = fc;
    
    float omega_c = 2.0f * M_PI * fc / fs;
    // 二阶贝塞尔：ζ = √3/2 ≈ 0.8660, ω0 = 1 rad/s (归一化)
    float zeta = 0.86602540378f;  // √3/2
    float omega0 = 1.0f;  // 归一化
    
    // 预扭曲
    float T = 1.0f / fs;
    float warped_omega0 = 2.0f * tan(omega0 * omega_c / 2.0f) / T;
    
    // 双线性变换系数计算
    float K = warped_omega0 * T;
    float K2 = K * K;
    
    float a0 = 4.0f + 4.0f * zeta * K + K2;
    
    filter->b0 = K2 / a0;
    filter->b1 = 2.0f * filter->b0;
    filter->b2 = filter->b0;
    
    filter->a1 = (2.0f * K2 - 8.0f) / a0;
    filter->a2 = (4.0f - 4.0f * zeta * K + K2) / a0;

    bessel2_reset(filter, 0.0f);
}


void bessel2_reset(BesselFilter2nd* filter, float initial_value) {
    if(NULL == filter) {
        return;
    }
    filter->x1 = initial_value;
    filter->x2 = initial_value;
    filter->y1 = initial_value;
    filter->y2 = initial_value;
}


float bessel2_update(BesselFilter2nd* filter, float input) {
    if (NULL == filter) {
        return input;
    }
    // y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    float output = filter->b0 * input
                 + filter->b1 * filter->x1
                 + filter->b2 * filter->x2
                 - filter->a1 * filter->y1
                 - filter->a2 * filter->y2;

    filter->x2 = filter->x1;
    filter->x1 = input;
    filter->y2 = filter->y1;
    filter->y1 = output;
    return output;
}


/**
 * 获取滤波器频率响应
 * @param filter 滤波器结构体指针
 * @param frequency 频率 (Hz)
 * @return 该频率下的增益 (dB)
 */
float bessel2_get_response_dB(BesselFilter2nd* filter, float frequency) {
    if (!filter || frequency < 0 || frequency > filter->fs/2) {
        return 0.0f;
    }
    
    // 计算频率响应的幅度
    float omega = 2.0f * M_PI * frequency / filter->fs;
    
    // 使用欧拉公式计算频率响应
    // H(z) = (b0 + b1*z?1 + b2*z?2) / (1 + a1*z?1 + a2*z?2)
    // 其中 z = e^(jω)
    
    float cos_omega = cosf(omega);
    float sin_omega = sinf(omega);
    
    // 分子
    float num_real = filter->b0 
                   + filter->b1 * cos_omega
                   + filter->b2 * cosf(2.0f * omega);
    float num_imag = -filter->b1 * sin_omega
                     - filter->b2 * sinf(2.0f * omega);
    
    // 分母
    float den_real = 1.0f
                   + filter->a1 * cos_omega
                   + filter->a2 * cosf(2.0f * omega);
    float den_imag = -filter->a1 * sin_omega
                     - filter->a2 * sinf(2.0f * omega);
    
    // 计算幅度
    float magnitude = sqrtf(num_real*num_real + num_imag*num_imag) 
                    / sqrtf(den_real*den_real + den_imag*den_imag);
    
    // 转换为dB
    return 20.0f * log10f(magnitude);
}


/**
 * 获取阶跃响应（模拟单位阶跃输入的输出）
 * @param filter 滤波器结构体指针
 * @param num_samples 采样点数
 * @param response 响应数组
 */
void bessel2_step_response(BesselFilter2nd* filter, uint32_t num_samples, float* response) {
    if (!filter || !response || num_samples == 0) return;
    
    BesselFilter2nd temp_filter = *filter;
    bessel2_reset(&temp_filter, 0.0f);
    
    for (uint32_t i = 0; i < num_samples; i++) {
        response[i] = bessel2_update(&temp_filter, 1.0f);
    }
}

