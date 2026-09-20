#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "linear_slope.h"

// 初始化滑动窗口
void sliding_window_init(sliding_window_LSQ_t* window) {
    window->write_idx = 0;
    window->count = 0;
    window->sum_y = 0.0f;
    window->sum_xy = 0.0f;
    window->slope = 0.0f;
    window->r_squared = 0.0f;
    window->n = 0.0f;
    
    // 预计算x值（秒为单位）和统计量
    window->sum_x = 0.0f;
    window->sum_x2 = 0.0f;
    
    for(uint16_t i = 0; i < SLIDING_WINDOW_SIZE; i++) {
        window->x_values[i] = i * SAMPLE_INTERVAL_S;  // 0.0, 0.025, 0.050, ...
        window->data[i] = 0.0f;
        
        // 预计算固定统计量
        window->sum_x += window->x_values[i];
        window->sum_x2 += window->x_values[i] * window->x_values[i];
    }
}


// 向窗口添加新数据点
void sliding_window_add_data(sliding_window_LSQ_t* window, float y_value) {
    uint16_t oldest_idx = window->write_idx;
    float oldest_y = window->data[oldest_idx];
    
    // 如果窗口已满，需要移除最旧的数据
    if(window->count == SLIDING_WINDOW_SIZE) {
        window->sum_y -= oldest_y;
        window->sum_xy -= window->x_values[oldest_idx] * oldest_y;
    } else {
        window->count++;
        window->n = (float)window->count;
    }
    
    // 添加新数据
    window->data[oldest_idx] = y_value;
    window->sum_y += y_value;
    
    // 更新Σxy
    window->sum_xy += window->x_values[oldest_idx] * y_value;
    
    // 更新写索引
    window->write_idx = (window->write_idx + 1) % SLIDING_WINDOW_SIZE;
    
    // 计算当前窗口的统计量
    if(window->count > 1) {
        calculate_slope(window);
    }
}


// 计算最小二乘斜率
void calculate_slope(sliding_window_LSQ_t* window) {
    if(window->count < 2) {
        window->slope = 0.0f;
        window->r_squared = 0.0f;
        return;
    }
    
    // 当前窗口的实际x统计量
    float current_sum_x = 0.0f;
    float current_sum_x2 = 0.0f;
    
    // 计算当前窗口中实际使用的x统计量
    // 注意：当窗口不满时，我们只使用前count个x值
    for(uint16_t i = 0; i < window->count; i++) {
        uint16_t idx = (window->write_idx - window->count + i + SLIDING_WINDOW_SIZE) % SLIDING_WINDOW_SIZE;
        current_sum_x += window->x_values[i];  // 注意：这里用i，因为x_values是从0开始的
        current_sum_x2 += window->x_values[i] * window->x_values[i];
    }
    
    // 计算分母
    float denominator = window->n * current_sum_x2 - current_sum_x * current_sum_x;
    
    // 防止除零
    if(fabsf(denominator) < 1e-10f) {
        window->slope = 0.0f;
        window->r_squared = 0.0f;
        return;
    }
    
    // 计算斜率: slope = (n*Σxy - Σx*Σy) / (n*Σx2 - (Σx)2)
    window->slope = (window->n * window->sum_xy - current_sum_x * window->sum_y) / denominator;
    
    // 可选：计算决定系数R2
    calculate_r_squared(window, current_sum_x);
}


// 计算决定系数R2
void calculate_r_squared(sliding_window_LSQ_t* window, float sum_x) {
    if(window->count < 2) {
        window->r_squared = 0.0f;
        return;
    }
    
    // 计算y的均值
    float y_mean = window->sum_y / window->n;
    
    // 计算总平方和SStot
    float ss_tot = 0.0f;
    for(uint16_t i = 0; i < window->count; i++) {
        uint16_t idx = (window->write_idx - window->count + i + SLIDING_WINDOW_SIZE) % SLIDING_WINDOW_SIZE;
        float y = window->data[idx];
        float diff = y - y_mean;
        ss_tot += diff * diff;
    }
    
    // 计算残差平方和SSres
    float ss_res = 0.0f;
    for(uint16_t i = 0; i < window->count; i++) {
        uint16_t idx = (window->write_idx - window->count + i + SLIDING_WINDOW_SIZE) % SLIDING_WINDOW_SIZE;
        float y = window->data[idx];
        float x = window->x_values[i];
        
        // 拟合值: y_fit = slope * x + intercept
        // 先计算截距: intercept = (Σy - slope * Σx) / n
        float intercept = (window->sum_y - window->slope * sum_x) / window->n;
        float y_fit = window->slope * x + intercept;
        
        float residual = y - y_fit;
        ss_res += residual * residual;
    }
    
    // 防止除零
    if(ss_tot < 1e-10f) {
        window->r_squared = 0.0f;
        return;
    }
    
    window->r_squared = 1.0f - ss_res / ss_tot;
}


// 获取当前斜率
float get_current_slope(const sliding_window_LSQ_t* window) {
    return window->slope;
}


// 获取当前决定系数
float get_current_r_squared(const sliding_window_LSQ_t* window) {
    return window->r_squared;
}


// 获取有效数据点数
uint16_t get_valid_count(const sliding_window_LSQ_t* window) {
    return window->count;
}
