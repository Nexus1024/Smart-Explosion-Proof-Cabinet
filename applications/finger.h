// applications/finger.h
#ifndef __FINGER_H__
#define __FINGER_H__

#include <rtthread.h>

/* 舵机配置 - 针对PE14引脚(TIM1_CH4) */
#define SERVO_UNLOCK_ANGLE  0           // 解锁角度(实际0°对应锁定位置)
#define SERVO_LOCK_ANGLE    90          // 锁定角度(实际90°对应解锁位置)

/* 配置 KEY 输入引脚 */
#define PIN_KEY0   GET_PIN(C, 0)   // PC0: KEY0 --> KEY
#define PIN_KEY1   GET_PIN(C, 1)   // PC1: KEY1 --> KEY
#define PIN_KEY2   GET_PIN(C, 4)   // PC4: KEY2 --> KEY
#define PIN_KEY3   GET_PIN(C, 5)   // PC5: WK_UP --> KEY3

/* ============================ msg.c 回调接口 ============================ */

/* 指纹操作数据结构 */
typedef struct {
    uint16_t finger_id;        // 指纹ID，固定为0x00
    uint8_t  operation_type;   // 操作类型：0=存物，1=取物
} finger_operation_t;

/* 指纹操作完成回调函数指针类型 */
typedef void (*finger_callback_t)(finger_operation_t *op_data);

/* 注册回调函数 */
void finger_register_callback(finger_callback_t callback);

/* ====================================================================== */

/**
 * @brief 创建并启动指纹识别线程
 *
 * 此函数会创建一个名为"finger"的线程，用于运行指纹识别、舵机控制
 * 和按键检测的主循环。线程优先级为20，适合需要快速响应的任务。
 *
 * @return rt_err_t 返回执行状态
 *         - RT_EOK: 线程创建并启动成功
 *         - 其他: 线程创建或启动失败
 */
rt_err_t finger_thread_start(void);

/**
 * @brief 停止并销毁指纹识别线程
 *
 * 此函数会设置停止标志，等待指纹线程安全退出，然后销毁线程。
 * 用于系统关机或模块重启时清理资源。
 *
 * @return rt_err_t 返回执行状态
 *         - RT_EOK: 线程停止并销毁成功
 *         - 其他: 线程不存在或停止失败
 */
rt_err_t finger_thread_stop(void);

/**
 * @brief 舵机解锁
 *
 * 控制舵机转动到解锁角度（0度），打开储物柜门。
 * 由于您的舵机方向相反，0度实际对应物理上的锁定位置。
 *
 * @return rt_err_t 返回执行状态
 *         - RT_EOK: 解锁成功
 *         - 其他: 解锁失败
 */
rt_err_t servo_unlock(void);

/**
 * @brief 舵机锁定
 *
 * 控制舵机转动到锁定角度（90度），关闭储物柜门。
 * 由于您的舵机方向相反，90度实际对应物理上的解锁位置。
 *
 * @return rt_err_t 返回执行状态
 *         - RT_EOK: 锁定成功
 *         - 其他: 锁定失败
 */
rt_err_t servo_lock(void);

/**
 * @brief 向后兼容的原始入口函数
 *
 * 此函数仅为保持旧代码兼容性而存在，实际功能与
 * finger_thread_start() 完全相同。新代码建议直接调用
 * finger_thread_start() 以获得更清晰的语义。
 *
 * @return int 返回执行状态（0表示成功）
 */
int finger(void);

#endif /* __FINGER_H__ */
