#ifndef __LINEAR_SLOPE_H__
#define __LINEAR_SLOPE_H__

#include "stdint.h"

// 配置参数
#define SLIDING_WINDOW_SIZE         160  // 4秒窗口，25ms间隔 (4/0.025=160)
#define SAMPLE_INTERVAL_MS          25.0f
#define SAMPLE_INTERVAL_S           (SAMPLE_INTERVAL_MS / 1000)

// 数据结构
typedef struct {
    float data[SLIDING_WINDOW_SIZE];  // 数据缓冲区
    uint16_t write_idx;               // 写索引
    uint16_t count;                   // 有效数据计数
    float x_values[SLIDING_WINDOW_SIZE];  // 固定的x值（秒为单位）
    // 预计算的x值统计量
    float sum_x;     // Σx
    float sum_x2;    // Σx2
    float n;         // 当前窗口中的数据点数
    // 运行时统计量
    float sum_y;     // Σy
    float sum_xy;    // Σxy
    // 斜率计算结果
    float slope;      // 当前斜率
    float r_squared;  // 决定系数
} sliding_window_LSQ_t;


void sliding_window_init(sliding_window_LSQ_t* window);

void sliding_window_add_data(sliding_window_LSQ_t* window, float y_value);

void calculate_slope(sliding_window_LSQ_t* window);

void calculate_r_squared(sliding_window_LSQ_t* window, float sum_x);

float get_current_slope(const sliding_window_LSQ_t* window);

float get_current_r_squared(const sliding_window_LSQ_t* window);

uint16_t get_valid_count(const sliding_window_LSQ_t* window);

#endif
