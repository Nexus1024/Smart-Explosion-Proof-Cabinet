#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <time.h>
#include "lcd.h"
#include "rfid.h"

// 指纹模块使用的串口名称
#define SAMPLE_UART_NAME "uart3"

/* 指纹识别等待参数：避免第一次指令无响应时空等10秒 */
#define FINGER_VERIFY_RETRY_COUNT       3
#define FINGER_RESPONSE_TIMEOUT_MS      3000
#define FINGER_RETRY_PROMPT_DELAY_MS    500

/* 舵机配置 - 针对PE14引脚(TIM1_CH4) */
#define SERVO_PWM_DEVICE    "pwm1"      // PWM设备
#define SERVO_PWM_CHANNEL   4           // TIM1_CH4对应通道4
// 由于舵机方向反了，我们需要调整角度定义
#define SERVO_UNLOCK_ANGLE  0           // 解锁角度(实际0°对应锁定位置)
#define SERVO_LOCK_ANGLE    90          // 锁定角度(实际90°对应解锁位置)

// SG90舵机PWM参数
#define PWM_PERIOD_NS       20000000    // 20ms = 20,000,000ns
#define PWM_MIN_PULSE_NS    500000      // 0.5ms = 500,000ns (舵机的180°位置)
#define PWM_MAX_PULSE_NS    2500000     // 2.5ms = 2,500,000ns (舵机的0°位置)

/* 自动验证指纹指令 */
unsigned char mode1[17] = {
    0xEF, 0x01, 0xFF, 0xFF, 0xFF,
    0xFF, 0x01, 0x00, 0x08, 0x32,
    0x02, 0xFF, 0xFF, 0x00, 0x01,
    0x02, 0x3C
};

/* 配置 LED 灯引脚 */
#define PIN_LED_B  GET_PIN(F, 11)  // PF11: LED_B --> LED
#define PIN_LED_R  GET_PIN(F, 12)  // PF12: LED_R --> LED

/* 配置 KEY 输入引脚 */
#define PIN_KEY0   GET_PIN(C, 0)   // PC0: KEY0 --> KEY
#define PIN_KEY1   GET_PIN(C, 1)   // PC1: KEY1 --> KEY
#define PIN_KEY2   GET_PIN(C, 4)   // PC4: KEY2 --> KEY
#define PIN_KEY3   GET_PIN(C, 5)   // PC5: WK_UP --> KEY3

// 串口设备句柄声明
static rt_device_t serial = RT_NULL;
// PWM设备句柄
static struct rt_device_pwm *servo_pwm = RT_NULL;
// 舵机状态
static rt_bool_t is_locked = RT_TRUE;
// 指纹验证状态
static rt_bool_t finger_verifying = RT_FALSE;
// 操作类型：0-存物，1-取物
static rt_uint8_t operation_type = 0;
// 解锁后RFID扫描状态
static rt_bool_t unlock_rfid_mode = RT_FALSE;
// 解锁后状态
static rt_bool_t is_unlocked = RT_FALSE;

// 线程控制变量
static rt_thread_t finger_thread = RT_NULL;
static rt_bool_t finger_thread_running = RT_FALSE;

/* 新增：回调函数相关定义 */
typedef struct {
    uint16_t finger_id;        // 指纹ID，使用模块返回的真实ID
    uint8_t  operation_type;   // 操作类型：0=存物，1=取物
} finger_operation_t;

typedef void (*finger_callback_t)(finger_operation_t *op_data);
static finger_callback_t finger_callback = RT_NULL;

/* 注册回调函数 */
void finger_register_callback(finger_callback_t callback)
{
    if (callback != RT_NULL) {
        finger_callback = callback;
        rt_kprintf("[Finger] Callback registered\n");
    }
}

/* 触发回调函数 */
static void trigger_finger_callback(uint16_t finger_id)
{
    if (finger_callback != RT_NULL) {
        finger_operation_t op_data = {
            .finger_id = finger_id,
            .operation_type = operation_type
        };
        finger_callback(&op_data);
        rt_kprintf("[Finger] Callback triggered: finger_id=%d, op_type=%d\n",
                   finger_id, operation_type);
    }
}

/* 将角度转换为PWM脉宽（纳秒）- 修正版本，因为舵机方向反了 */
static rt_uint32_t angle_to_pulse_ns(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    // 由于舵机方向反了，我们需要反转角度映射
    // 0度应该对应2500000ns (2.5ms)，180度应该对应500000ns (0.5ms)
    int reversed_angle = 180 - angle;

    return PWM_MIN_PULSE_NS + (reversed_angle * (PWM_MAX_PULSE_NS - PWM_MIN_PULSE_NS) / 180);
}

/* 控制舵机角度 */
static rt_err_t servo_set_angle(int angle)
{
    if (!servo_pwm) {
        return -RT_ERROR;
    }

    rt_uint32_t pulse_ns = angle_to_pulse_ns(angle);

    // 确保PWM已启用
    rt_pwm_enable(servo_pwm, SERVO_PWM_CHANNEL);

    // 设置PWM参数
    rt_pwm_set(servo_pwm, SERVO_PWM_CHANNEL, PWM_PERIOD_NS, pulse_ns);

    // 给舵机时间响应
    rt_thread_mdelay(200);

    return RT_EOK;
}

/* 舵机解锁 - 由于舵机方向反了，解锁是0度 */
static rt_err_t servo_unlock(void)
{
    // 设置舵机到解锁角度(0度)
    rt_err_t ret = servo_set_angle(SERVO_UNLOCK_ANGLE);
    if (ret == RT_EOK) {
        is_locked = RT_FALSE;
        is_unlocked = RT_TRUE;
        return RT_EOK;
    }
    return -RT_ERROR;
}

/* 舵机锁定 - 由于舵机方向反了，锁定是90度 */
static rt_err_t servo_lock(void)
{
    // 设置舵机到锁定角度(90度)
    rt_err_t ret = servo_set_angle(SERVO_LOCK_ANGLE);
    if (ret == RT_EOK) {
        is_locked = RT_TRUE;
        is_unlocked = RT_FALSE;
        return RT_EOK;
    }
    return -RT_ERROR;
}

/* 初始化舵机控制 */
static rt_err_t servo_init(void)
{
    servo_pwm = (struct rt_device_pwm *)rt_device_find(SERVO_PWM_DEVICE);
    if (!servo_pwm) {
        return -RT_ERROR;
    }

    // 设置初始位置为锁定(90度)
    rt_err_t ret = servo_set_angle(SERVO_LOCK_ANGLE);
    if (ret != RT_EOK) {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 执行RFID扫描流程 */
static void rfid_scan_procedure(void)
{
    rfid_result_t result = RFID_RESULT_ERROR;
    int wait_seconds = 10;

    // 显示10秒倒计时
    for (int i = wait_seconds; i > 0; i--) {
        char countdown_msg[32];
        rt_snprintf(countdown_msg, sizeof(countdown_msg), "Wait %d seconds", i);
        lcd_show_centered_message("RFID Scanning", countdown_msg);
        rt_thread_mdelay(1000);
    }

    // 显示RFID扫描中
    lcd_show_centered_message("RFID Scanning", "Please place tag...");

    // 执行RFID扫描
    result = rfid_scan_once();

    // 根据结果更新显示
    switch (result) {
        case RFID_RESULT_ITEM_1:
            lcd_show_centered_message("NO.1", "Dangerous Goods 1");
            rt_kprintf("RFID Scan Result: Dangerous Goods 1 (NO.1)\n");
            break;
        case RFID_RESULT_ITEM_2:
            lcd_show_centered_message("NO.2", "Dangerous Goods 2");
            rt_kprintf("RFID Scan Result: Dangerous Goods 2 (NO.2)\n");
            break;
        case RFID_RESULT_UNKNOWN:
            lcd_show_centered_message("UNKNOWN", "Unknown Item");
            rt_kprintf("RFID Scan Result: Unknown Item\n");
            break;
        case RFID_RESULT_NONE:
            lcd_show_centered_message("NO TAG", "Please place tag");
            rt_kprintf("RFID Scan Result: No tag detected\n");
            break;
        case RFID_RESULT_ERROR:
        default:
            lcd_show_centered_message("RFID ERROR", "Scan Failed");
            rt_kprintf("RFID Scan Result: Error\n");
            break;
    }

    // 等待2秒让用户查看结果
    rt_thread_mdelay(2000);

    // 返回到解锁状态界面
    lcd_show_centered_message("Press KEY2 to scan", "Long press KEY2 to lock");
}

/* 清空串口接收缓冲区 */
static void clear_serial_buffer(void)
{
    if (!serial) return;

    rt_uint8_t dummy[64];
    rt_size_t read_len;

    do {
        read_len = rt_device_read(serial, 0, dummy, sizeof(dummy));
    } while (read_len > 0);
}

/* 校验指纹模块响应包校验和 */
static rt_bool_t verify_fingerprint_checksum(rt_uint8_t *packet, int packet_len)
{
    if (packet_len < 12) return RT_FALSE;

    rt_uint16_t checksum = 0;
    rt_uint16_t received_checksum = (packet[packet_len - 2] << 8) | packet[packet_len - 1];

    // 校验和从包标识开始累加，包括包长度和数据，不包括最后2字节校验和
    for (int i = 6; i < packet_len - 2; i++) {
        checksum += packet[i];
    }

    return checksum == received_checksum;
}

/* 指纹自动验证解析结果 */
typedef enum {
    FINGER_PACKET_OTHER = 0,       // 中间过程包或无关包，继续等待
    FINGER_PACKET_MATCH_SUCCESS,   // 明确匹配成功，可以开门
    FINGER_PACKET_MATCH_FAILED     // 明确匹配失败，不能开门，可重新验证
} finger_packet_result_t;

/* 前向声明：失败重试时需要重新下发自动验证指令 */
static rt_err_t finger_send_command(void);

/* 解析指纹响应包：区分“匹配成功 / 匹配失败 / 继续等待” */
static finger_packet_result_t parse_fingerprint_packet(rt_uint8_t *packet, int packet_len,
                                                       uint16_t *finger_id, uint16_t *match_score)
{
    if (packet_len < 17) return FINGER_PACKET_OTHER;

    // 检查包头和应答包标识
    if (packet[0] != 0xEF || packet[1] != 0x01) return FINGER_PACKET_OTHER;
    if (packet[6] != 0x07) return FINGER_PACKET_OTHER;

    // 自动验证指纹PS_AutoIdentify的应答包长度应为0x0008
    rt_uint16_t pkg_len = (packet[7] << 8) | packet[8];
    if (pkg_len != 0x0008) return FINGER_PACKET_OTHER;

    // 校验数据完整性，避免错误帧被误判为验证成功
    if (!verify_fingerprint_checksum(packet, packet_len)) return FINGER_PACKET_OTHER;

    rt_uint8_t confirm_code = packet[9];
    rt_uint8_t param = packet[10];

    // 只有确认码=0x00且参数=0x05，才表示最终搜索/比对成功
    if (confirm_code == 0x00 && param == 0x05) {
        *finger_id = (packet[11] << 8) | packet[12];
        *match_score = (packet[13] << 8) | packet[14];

        // 0xFFFF是指令中的1:N搜索目标值，不应作为匹配到的真实用户ID
        if (*finger_id == 0xFFFF) {
            rt_kprintf("[Finger] Invalid matched finger_id=0xFFFF\n");
            return FINGER_PACKET_MATCH_FAILED;
        }

        rt_kprintf("[Finger] Match success: finger_id=%d, score=%d\n", *finger_id, *match_score);
        return FINGER_PACKET_MATCH_SUCCESS;
    }

    // 自动验证过程中的中间步骤包：确认码为0，但参数还没到0x05，继续等待最终结果
    if (confirm_code == 0x00 && param != 0x05) {
        return FINGER_PACKET_OTHER;
    }

    // 其他确认码表示本次验证没有成功，例如不匹配、未搜索到、采集超时等
    rt_kprintf("[Finger] Match failed: confirm=0x%02X, param=0x%02X\n",
               confirm_code, param);
    return FINGER_PACKET_MATCH_FAILED;
}

/* LED闪烁提示 */
static void led_blink_pattern(int times, int on_ms, int off_ms)
{
    for (int i = 0; i < times; i++) {
        rt_pin_write(PIN_LED_R, PIN_LOW);
        rt_thread_mdelay(on_ms);
        rt_pin_write(PIN_LED_R, PIN_HIGH);
        rt_thread_mdelay(off_ms);
    }
}

/* 等待一次指纹自动验证结果 */
static finger_packet_result_t finger_wait_response_once(uint16_t *finger_id, uint16_t *match_score)
{
    rt_uint8_t buffer[256];
    rt_size_t total_read = 0;
    rt_tick_t start_time = rt_tick_get();
    const rt_tick_t timeout = rt_tick_from_millisecond(FINGER_RESPONSE_TIMEOUT_MS);

    // 使用LED常亮指示等待状态
    rt_pin_write(PIN_LED_R, PIN_LOW);

    // 持续读取直到超时、匹配成功或明确匹配失败
    while ((rt_tick_get() - start_time) < timeout) {
        rt_size_t read_len = rt_device_read(serial, 0, buffer + total_read, sizeof(buffer) - total_read);
        if (read_len > 0) {
            total_read += read_len;

            // 尝试解析接收到的数据
            int pos = 0;
            while (pos < total_read) {
                // 查找包头
                if (pos + 1 < total_read && buffer[pos] == 0xEF && buffer[pos + 1] == 0x01) {
                    // 检查包长度字段是否已经收到
                    if (pos + 8 < total_read) {
                        uint16_t pkg_len = (buffer[pos + 7] << 8) | buffer[pos + 8];
                        uint16_t full_pkt_len = 9 + pkg_len;

                        if (pos + full_pkt_len <= total_read) {
                            // 完整包已接收
                            finger_packet_result_t result = parse_fingerprint_packet(&buffer[pos], full_pkt_len,
                                                                                     finger_id, match_score);
                            if (result == FINGER_PACKET_MATCH_SUCCESS || result == FINGER_PACKET_MATCH_FAILED) {
                                return result;
                            }

                            // 中间过程包或无关包，跳过继续找最终结果包
                            pos += full_pkt_len;
                            continue;
                        }
                    }
                }
                pos++;
            }

            // 避免接收缓冲区被无效数据填满
            if (total_read > sizeof(buffer) - 32) {
                total_read = 0;
            }
        }

        rt_thread_mdelay(10);
    }

    rt_kprintf("[Finger] No valid response within %d ms\n", FINGER_RESPONSE_TIMEOUT_MS);
    return FINGER_PACKET_MATCH_FAILED;
}

/* 接收指纹模块响应，失败后允许重新放置手指再次判断 */
static void finger_receive_response(void)
{
    uint16_t finger_id = 0xFFFF;
    uint16_t match_score = 0;

    for (int attempt = 1; attempt <= FINGER_VERIFY_RETRY_COUNT; attempt++) {
        finger_packet_result_t result;

        /*
         * 每一次尝试都重新下发自动验证指令。
         * 这样第一次指令如果因为模块未就绪/串口瞬时异常没有真正触发，
         * 不会再等满10秒才重新开始识别。
         */
        if (attempt > 1) {
            lcd_show_centered_message("Please place", "your finger...");
        }

        if (finger_send_command() != RT_EOK) {
            rt_pin_write(PIN_LED_R, PIN_HIGH);
            lcd_set_state(LCD_STATE_WELCOME);
            finger_verifying = RT_FALSE;
            return;
        }

        rt_kprintf("[Finger] Verify attempt %d/%d\n", attempt, FINGER_VERIFY_RETRY_COUNT);
        result = finger_wait_response_once(&finger_id, &match_score);

        if (result == FINGER_PACKET_MATCH_SUCCESS) {
            rt_pin_write(PIN_LED_R, PIN_HIGH);  // 关闭LED

            // 只有明确匹配成功才解锁舵机
            servo_unlock();

            // 指纹验证成功，更新LCD显示
            lcd_set_state(LCD_STATE_SUCCESS);

            // 触发回调函数，传递真实指纹ID和操作类型
            trigger_finger_callback(finger_id);

            // 进入解锁后RFID扫描模式
            unlock_rfid_mode = RT_TRUE;

            // 指纹验证完成
            finger_verifying = RT_FALSE;
            return;
        }

        // 本次识别失败或无有效响应：不开门，提示用户重新放置手指
        rt_pin_write(PIN_LED_R, PIN_HIGH);
        led_blink_pattern(2, 150, 150);

        if (attempt < FINGER_VERIFY_RETRY_COUNT) {
            lcd_show_centered_message("Finger Failed", "Place again...");
            rt_thread_mdelay(FINGER_RETRY_PROMPT_DELAY_MS);
        }
    }

    // 多次失败后返回初始界面，防止一直卡在指纹验证界面
    lcd_set_state(LCD_STATE_FAILED);
    rt_thread_mdelay(1000);
    lcd_set_state(LCD_STATE_WELCOME);

    finger_verifying = RT_FALSE;
}

/* 初始化指纹模块 */
static rt_err_t finger_init(void)
{
    serial = rt_device_find(SAMPLE_UART_NAME);
    if (serial == RT_NULL) {
        return -RT_ERROR;
    }

    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = BAUD_RATE_57600;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity = PARITY_NONE;

    rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config);

    if (rt_device_open(serial, RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 发送指纹验证指令 */
static rt_err_t finger_send_command(void)
{
    if (serial == RT_NULL) {
        return -RT_ERROR;
    }

    // 清空缓冲区，给模块和串口驱动一个很短的稳定间隔
    clear_serial_buffer();
    rt_thread_mdelay(20);

    rt_size_t written = rt_device_write(serial, 0, mode1, sizeof(mode1));
    if (written != sizeof(mode1)) {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 检测按键长按 */
static rt_bool_t check_key_long_press(int pin, int timeout_ms)
{
    rt_tick_t start_time = rt_tick_get();
    const rt_tick_t timeout = rt_tick_from_millisecond(timeout_ms);

    // 等待按键稳定按下
    rt_thread_mdelay(20);

    if (rt_pin_read(pin) != PIN_LOW) {
        return RT_FALSE;  // 按键已释放
    }

    // 检测长按
    while (rt_tick_get() - start_time < timeout) {
        if (rt_pin_read(pin) != PIN_LOW) {
            return RT_FALSE;  // 按键在超时前释放，不是长按
        }
        rt_thread_mdelay(10);
    }

    // 达到长按时长
    return RT_TRUE;
}

/* 指纹线程入口函数 */
static void finger_thread_entry(void *parameter)
{
    // 初始化舵机
    servo_init();

    // 初始化指纹模块
    finger_init();

    // 初始化RFID模块
    rt_kprintf("Initializing RFID module...\n");
    if (rfid_module_init() != RT_EOK) {
        rt_kprintf("Warning: RFID module initialization failed\n");
    } else {
        rt_kprintf("RFID module initialized successfully\n");
    }

    // 初始化引脚
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_KEY1, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_KEY2, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_KEY3, PIN_MODE_INPUT_PULLUP);

    // 显示初始界面
    lcd_set_state(LCD_STATE_WELCOME);

    while (finger_thread_running) {
        // 检测KEY0按键 - 取物操作
        if (rt_pin_read(PIN_KEY0) == PIN_LOW) {
            rt_thread_mdelay(50);

            if (rt_pin_read(PIN_KEY0) == PIN_LOW) {
                // 防止重复触发
                if (finger_verifying) {
                    // LED快速闪烁表示忙
                    led_blink_pattern(3, 100, 100);

                    // 等待按键释放
                    while (rt_pin_read(PIN_KEY0) == PIN_LOW) {
                        rt_thread_mdelay(10);
                    }
                    continue;
                }

                // 解锁状态下，退出RFID扫描模式，返回欢迎界面
                if (is_unlocked) {
                    unlock_rfid_mode = RT_FALSE;
                    lcd_set_state(LCD_STATE_WELCOME);

                    // 不锁定舵机，直接进入指纹识别流程
                }

                // 设置操作类型为取物
                operation_type = 1;

                // 更新LCD显示：等待放置手指
                lcd_set_state(LCD_STATE_WAIT_FINGER);

                // 标记开始验证
                finger_verifying = RT_TRUE;

                // 指纹验证函数内部会立即发送验证指令，并在无响应时快速重发
                finger_receive_response();

                // 等待按键释放
                while (rt_pin_read(PIN_KEY0) == PIN_LOW) {
                    rt_thread_mdelay(10);
                }
            }
        }

        // 检测KEY1按键 - 存物操作
        if (rt_pin_read(PIN_KEY1) == PIN_LOW) {
            rt_thread_mdelay(50);

            if (rt_pin_read(PIN_KEY1) == PIN_LOW) {
                // 防止重复触发
                if (finger_verifying) {
                    // LED快速闪烁表示忙
                    led_blink_pattern(3, 100, 100);

                    // 等待按键释放
                    while (rt_pin_read(PIN_KEY1) == PIN_LOW) {
                        rt_thread_mdelay(10);
                    }
                    continue;
                }

                // 解锁状态下，退出RFID扫描模式，返回欢迎界面
                if (is_unlocked) {
                    unlock_rfid_mode = RT_FALSE;
                    lcd_set_state(LCD_STATE_WELCOME);

                    // 不锁定舵机，直接进入指纹识别流程
                }

                // 设置操作类型为存物
                operation_type = 0;

                // 更新LCD显示：等待放置手指
                lcd_set_state(LCD_STATE_WAIT_FINGER);

                // 标记开始验证
                finger_verifying = RT_TRUE;

                // 指纹验证函数内部会立即发送验证指令，并在无响应时快速重发
                finger_receive_response();

                // 等待按键释放
                while (rt_pin_read(PIN_KEY1) == PIN_LOW) {
                    rt_thread_mdelay(10);
                }
            }
        }

        // 检测KEY2按键 - 解锁状态下有两种功能
        if (rt_pin_read(PIN_KEY2) == PIN_LOW) {
            rt_thread_mdelay(50);

            if (rt_pin_read(PIN_KEY2) == PIN_LOW) {
                // 在解锁状态下
                if (is_unlocked) {
                    // 检测是否为长按（≥5秒）
                    if (check_key_long_press(PIN_KEY2, 5000)) {
                        // 长按5秒：锁定舵机
                        if (servo_lock() == RT_EOK) {
                            // 更新LCD显示：回到初始界面
                            lcd_set_state(LCD_STATE_WELCOME);

                            // 退出解锁后RFID扫描模式
                            unlock_rfid_mode = RT_FALSE;

                            // LED闪烁1次表示锁定成功
                            rt_pin_write(PIN_LED_R, PIN_LOW);
                            rt_thread_mdelay(200);
                            rt_pin_write(PIN_LED_R, PIN_HIGH);
                        } else {
                            // 锁定失败显示错误信息
                            lcd_show_centered_message("Lock Failed", "Try Again");
                        }
                    } else {
                        // 短按：执行RFID扫描
                        rfid_scan_procedure();
                    }

                    // 等待按键释放
                    while (rt_pin_read(PIN_KEY2) == PIN_LOW) {
                        rt_thread_mdelay(10);
                    }
                } else {
                    // 锁定状态下，短按KEY2锁定舵机
                    if (servo_lock() == RT_EOK) {
                        // 更新LCD显示：回到初始界面
                        lcd_set_state(LCD_STATE_WELCOME);

                        // LED闪烁1次表示锁定成功
                        rt_pin_write(PIN_LED_R, PIN_LOW);
                        rt_thread_mdelay(200);
                        rt_pin_write(PIN_LED_R, PIN_HIGH);
                    } else {
                        // 锁定失败显示错误信息
                        lcd_show_centered_message("Lock Failed", "Try Again");
                    }

                    // 等待按键释放
                    while (rt_pin_read(PIN_KEY2) == PIN_LOW) {
                        rt_thread_mdelay(10);
                    }
                }
            }
        }

        // 检测KEY3按键 - 固定触发显示ERROR
        if (rt_pin_read(PIN_KEY3) == PIN_LOW) {
            rt_thread_mdelay(50);

            if (rt_pin_read(PIN_KEY3) == PIN_LOW) {
                // 显示ERROR界面
                lcd_set_state(LCD_STATE_FAILED);

                // 等待2秒
                rt_thread_mdelay(2000);

                // 返回到初始界面
                lcd_set_state(LCD_STATE_WELCOME);

                // 等待按键释放
                while (rt_pin_read(PIN_KEY3) == PIN_LOW) {
                    rt_thread_mdelay(10);
                }
            }
        }

        rt_thread_mdelay(10);
    }
}

/* 创建并启动指纹线程 */
rt_err_t finger_thread_start(void)
{
    if (finger_thread != RT_NULL) {
        return RT_EOK;  // 线程已存在
    }

    finger_thread_running = RT_TRUE;

    finger_thread = rt_thread_create("finger",
                                     finger_thread_entry,
                                     RT_NULL,
                                     2048,  // 堆栈大小
                                     20,    // 优先级
                                     10);   // 时间片

    if (finger_thread != RT_NULL) {
        rt_thread_startup(finger_thread);
        return RT_EOK;
    }

    finger_thread_running = RT_FALSE;
    return -RT_ERROR;
}

/* 停止指纹线程 */
rt_err_t finger_thread_stop(void)
{
    if (finger_thread == RT_NULL) {
        return RT_EOK;  // 线程不存在
    }

    finger_thread_running = RT_FALSE;

    // 等待线程退出
    rt_thread_mdelay(100);

    // 销毁线程
    rt_thread_delete(finger_thread);
    finger_thread = RT_NULL;

    return RT_EOK;
}

/* 原来的finger函数，用于向后兼容 */
int finger(void)
{
    return finger_thread_start();
}
