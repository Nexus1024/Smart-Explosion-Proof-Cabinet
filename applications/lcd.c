// applications/lcd.c
#include "lcd.h"
#include <drv_lcd.h>
#include <rtthread.h>
#include <string.h>

// 屏幕参数
#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   240

// 字体大小定义（像素）
#define FONT_SIZE_16    16
#define FONT_SIZE_24    24
#define FONT_SIZE_32    32

// 全局变量
// 初始值设为无效状态，避免首次调用 lcd_set_state(LCD_STATE_WELCOME) 时被误判为“状态未变化”而不绘制欢迎界面
static lcd_state_t current_state = (lcd_state_t)-1;
static rt_mutex_t lcd_mutex = RT_NULL;

// 内部函数声明
static void lcd_show_welcome(void);
static void lcd_show_wait_finger(void);
static void lcd_show_success(void);
static void lcd_show_failed(void);
static void lcd_show_press_to_lock(void);
static int calculate_centered_x(const char* str, uint8_t font_size);
static void draw_centered_string(uint16_t y, const char* str, uint8_t font_size);

// 初始化LCD显示模块
void lcd_display_init(void)
{
    // 创建互斥锁，防止多任务同时访问LCD
    lcd_mutex = rt_mutex_create("lcd_mutex", RT_IPC_FLAG_FIFO);
    if (lcd_mutex == RT_NULL) {
        return;
    }

    // 设置默认颜色
    rt_mutex_take(lcd_mutex, RT_WAITING_FOREVER);
    lcd_set_color(WHITE, BLACK);
    rt_mutex_release(lcd_mutex);
}

// 设置显示状态
void lcd_set_state(lcd_state_t state)
{
    if (lcd_mutex == RT_NULL) {
        return; // 未初始化
    }

    rt_mutex_take(lcd_mutex, RT_WAITING_FOREVER);

    if (state == current_state) {
        rt_mutex_release(lcd_mutex);
        return; // 状态未变化
    }

    current_state = state;

    // 清屏
    lcd_clear(WHITE);

    // 根据状态显示不同内容
    switch (state) {
        case LCD_STATE_WELCOME:
            lcd_show_welcome();
            break;

        case LCD_STATE_WAIT_FINGER:
            lcd_show_wait_finger();
            break;

        case LCD_STATE_SUCCESS:
            lcd_show_success();
            break;

        case LCD_STATE_FAILED:
            lcd_show_failed();
            break;

        case LCD_STATE_PRESS_TO_LOCK:
            lcd_show_press_to_lock();
            break;
    }

    rt_mutex_release(lcd_mutex);
}

// 清屏
void lcd_clear_screen(void)
{
    if (lcd_mutex == RT_NULL) {
        return;
    }

    rt_mutex_take(lcd_mutex, RT_WAITING_FOREVER);
    lcd_clear(WHITE);
    rt_mutex_release(lcd_mutex);
}

// 显示欢迎界面
static void lcd_show_welcome(void)
{
    // 设置颜色
    lcd_set_color(WHITE, BLACK);

    // 计算垂直居中位置
    uint16_t start_y = (SCREEN_HEIGHT - (2 * FONT_SIZE_16) - 10) / 2;

    // 第一行文字
    const char* line1 = "Press LEFT to take";
    draw_centered_string(start_y, line1, FONT_SIZE_16);

    // 第二行文字
    const char* line2 = "Press DOWN to store";
    draw_centered_string(start_y + FONT_SIZE_16 + 10, line2, FONT_SIZE_16);
}

// 显示等待放置手指界面
static void lcd_show_wait_finger(void)
{
    // 设置颜色
    lcd_set_color(WHITE, BLACK);

    // 单行文字，居中显示
    const char* message = "Please place your finger...";
    draw_centered_string(SCREEN_HEIGHT / 2 - FONT_SIZE_16 / 2,
                         message, FONT_SIZE_16);
}

// 显示验证成功界面
static void lcd_show_success(void)
{
    // 设置颜色（绿色表示成功）
    lcd_set_color(WHITE, BLACK);

    // 计算垂直位置
    uint16_t start_y = (SCREEN_HEIGHT - (2 * FONT_SIZE_16) - 10) / 2;

    // 第一行：PASS!
    draw_centered_string(start_y, "PASS!", FONT_SIZE_16);

    // 恢复颜色
    lcd_set_color(WHITE, BLACK);

    // 第二行：Press RIGHT to lock...
    draw_centered_string(start_y + FONT_SIZE_16 + 10,
                        "Press RIGHT to scan...", FONT_SIZE_16);
}

// 显示验证失败界面
static void lcd_show_failed(void)
{
    // 设置颜色（红色表示失败）
    lcd_set_color(RED, WHITE);

    // 单行文字，居中显示
    const char* message = "ERROR...";
    draw_centered_string(SCREEN_HEIGHT / 2 - FONT_SIZE_16 / 2,
                         message, FONT_SIZE_16);
}

// 显示按右键锁定提示
static void lcd_show_press_to_lock(void)
{
    // 设置颜色
    lcd_set_color(WHITE, BLACK);

    // 单行文字，居中显示
    const char* message = "Press RIGHT to lock...";
    draw_centered_string(SCREEN_HEIGHT / 2 - FONT_SIZE_16 / 2,
                         message, FONT_SIZE_16);
}

// 显示自定义消息（居中显示）
void lcd_show_centered_message(const char* line1, const char* line2)
{
    if (lcd_mutex == RT_NULL) {
        return;
    }

    rt_mutex_take(lcd_mutex, RT_WAITING_FOREVER);

    // 清屏
    lcd_clear(WHITE);
    lcd_set_color(WHITE, BLACK);

    if (line1 != RT_NULL) {
        draw_centered_string(SCREEN_HEIGHT / 2 - FONT_SIZE_16,
                            line1, FONT_SIZE_16);
    }

    if (line2 != RT_NULL) {
        draw_centered_string(SCREEN_HEIGHT / 2 + 5,
                            line2, FONT_SIZE_16);
    }

    rt_mutex_release(lcd_mutex);
}

// 计算字符串居中显示的X坐标
static int calculate_centered_x(const char* str, uint8_t font_size)
{
    if (str == RT_NULL) {
        return 0;
    }

    // 假设字符宽度：16号字体约8像素，24号约12像素，32号约16像素
    uint8_t char_width = 8;
    if (font_size == FONT_SIZE_24) {
        char_width = 12;
    } else if (font_size == FONT_SIZE_32) {
        char_width = 16;
    }

    int str_width = strlen(str) * char_width;
    int x = (SCREEN_WIDTH - str_width) / 2;

    return x > 0 ? x : 0;
}

// 绘制居中字符串
static void draw_centered_string(uint16_t y, const char* str, uint8_t font_size)
{
    int x = calculate_centered_x(str, font_size);
    lcd_show_string(x, y, font_size, (char*)str);
}
