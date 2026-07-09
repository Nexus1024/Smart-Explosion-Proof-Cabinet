#include <rtthread.h>
#include "applications/finger.h"
#include "applications/lcd.h"
//#include "applications/aht21.h"
#include "applications/rfid.h"
#include "applications/wifi.h"
#include "applications/onenet.h"
#include "applications/msg.h"

int main(void)
{
    wifi();          /* 先初始化WiFi */
    onenet_init();   /* 再初始化OneNET连接 */
    msg_module_init(); /* 最后初始化消息模块 */
    // 1. 初始化LCD显示模块
    lcd_display_init();
    /* 初始化消息模块 */
    msg_module_init();

    // 2. 启动指纹识别线程
    // 注意：原有的finger()函数现在会启动指纹线程
    // 但为了代码清晰，建议使用新的线程启动函数
    finger_thread_start();

//    rfid_scan_once();
    // 4. 主线程进入空闲循环
    // 主线程可以处理系统级任务，如看门狗喂狗、系统状态监控等
    while (1) {
        // 可在此处添加主线程需要执行的任务
        // 例如：定期检查系统状态、处理线程间协调等

        // 示例：每秒检查一次温度是否超过阈值
        // float current_temp = aht21_get_current_temperature();
        // if (current_temp > aht21_get_threshold()) {
        //     // 温度超过阈值，可以采取相应措施
        // }

        rt_thread_mdelay(1000);
    }

    return 0;
}
