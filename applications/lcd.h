// applications/lcd.h
#ifndef __LCD_DISPLAY_H__
#define __LCD_DISPLAY_H__

#include <rtthread.h>

// LCD显示状态枚举
typedef enum {
    LCD_STATE_WELCOME,          // 初始界面
    LCD_STATE_WAIT_FINGER,      // 等待放置手指
    LCD_STATE_SUCCESS,          // 指纹验证成功
    LCD_STATE_FAILED,           // 指纹验证失败
    LCD_STATE_PRESS_TO_LOCK     // 提示按右键锁定
} lcd_state_t;

// 初始化LCD显示模块
void lcd_display_init(void);

// 设置显示状态
void lcd_set_state(lcd_state_t state);

// 显示自定义消息（居中显示）
void lcd_show_centered_message(const char* line1, const char* line2);

// 清屏
void lcd_clear_screen(void);

#endif
