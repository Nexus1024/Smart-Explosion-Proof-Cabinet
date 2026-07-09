/* RFID模块实现 - 完整修复版本 */

#include "rfid.h"
#include "drv_rs485.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

/* 主动盘存命令 */
static const uint8_t RFID_ACTIVE_INVENTORY_CMD[] = {
    0x52, 0x46, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x46
};
#define CMD_FRAME_SIZE 9

/* 接收缓冲区 */
#define RX_BUF_SIZE 256
static uint8_t rx_buf[RX_BUF_SIZE];

/* 私有函数声明 */
static int find_valid_frame(const uint8_t *data, int len);
static rfid_result_t rfid_parse_inventory_response(const uint8_t *data, uint8_t len);
static rfid_result_t rfid_parse_response_impl(const uint8_t *data, uint8_t len);

/* 新增：回调函数相关定义 */
typedef void (*rfid_callback_t)(rfid_result_t result);
static rfid_callback_t rfid_callback = RT_NULL;

/* 注册RFID扫描回调函数 */
void rfid_register_callback(rfid_callback_t callback)
{
    if (callback != RT_NULL) {
        rfid_callback = callback;
        rt_kprintf("[RFID] Callback registered\n");
    }
}

/* 触发RFID回调函数 */
static void trigger_rfid_callback(rfid_result_t result)
{
    if (rfid_callback != RT_NULL) {
        rfid_callback(result);
        rt_kprintf("[RFID] Callback triggered: result=%d (%s)\n",
                   result, rfid_get_item_name(result));
    }
}

/* RFID模块初始化 */
rt_err_t rfid_module_init(void)
{
    rt_kprintf("Initializing RFID module...\n");

    /* 初始化RS485串口 - 驱动会自动初始化RTS引脚 */
    rt_err_t ret = rs485_init();
    if (ret != RT_EOK) {
        rt_kprintf("RFID: RS485 init failed\n");
        return ret;
    }

    /* 清空接收缓冲区 */
    uint8_t dummy[64];
    int cleared_bytes = 0;
    while (rt_device_read(rs485_serial, 0, dummy, sizeof(dummy)) > 0) {
        cleared_bytes++;
        rt_thread_mdelay(1);
    }
    if (cleared_bytes > 0) {
        rt_kprintf("RFID: Cleared %d bytes from buffer\n", cleared_bytes);
    }

    rt_kprintf("RFID: Module initialized successfully\n");
    return RT_EOK;
}

/* 查找有效帧起始位置 */
static int find_valid_frame(const uint8_t *data, int len)
{
    if (len < 2) {
        return -1;
    }

    /* 搜索帧头 0x52 0x46 */
    for (int i = 0; i <= len - 2; i++) {
        if (data[i] == 0x52 && data[i + 1] == 0x46) {
            return i;
        }
    }
    return -1;
}

/* 解析主动盘存响应（处理标签数据） */
static rfid_result_t rfid_parse_inventory_response(const uint8_t *data, uint8_t len)
{
    /* 解析主动盘存响应 */
    uint16_t data_len = (data[6] << 8) | data[7];
    const uint8_t *payload = &data[8];

    rt_kprintf("RFID: Parsing inventory response, data_len=0x%04X\n", data_len);

    /* 检查是否无标签 */
    if (data_len == 0x03) {
        rt_kprintf("RFID: No tag frame detected (data_len=0x03)\n");
        if (payload[0] == 0x07 && payload[1] == 0x01 && payload[2] == 0x50) {
            rt_kprintf("RFID: No tag detected\n");
            return RFID_RESULT_NONE;
        } else {
            rt_kprintf("RFID: Invalid no-tag data: %02X %02X %02X\n",
                      payload[0], payload[1], payload[2]);
            return RFID_RESULT_ERROR;
        }
    }

    /* 打印完整帧用于调试 */
    rt_kprintf("RFID: Complete frame (%d bytes): ", len);
    for (int i = 0; i < len && i < 32; i++) {
        rt_kprintf("%02X ", data[i]);
    }
    rt_kprintf("\n");

    /* 检查帧长度是否足够包含第15和16位（索引15和16） */
    if (len < 17) {  /* 需要至少17字节才能访问索引15和16 */
        rt_kprintf("RFID: Frame too short (%d bytes) to contain tag ID at position 15-16\n", len);

        /* 回退到原来的搜索逻辑 */
        rt_kprintf("RFID: Fallback to searching in payload...\n");

        /* 打印payload数据用于调试 */
        rt_kprintf("RFID: Payload data (%d bytes): ", data_len);
        for (int i = 0; i < data_len && i < 20; i++) {
            rt_kprintf("%02X ", payload[i]);
        }
        rt_kprintf("\n");

        /* 在payload中搜索NO.1或NO.2 */
        for (int i = 0; i < data_len - 1; i++) {
            if (payload[i] == 0x00 && payload[i + 1] == 0x01) {
                rt_kprintf("RFID: Found NO.1 tag at payload[%d,%d]\n", i, i+1);
                return RFID_RESULT_ITEM_1;
            } else if (payload[i] == 0x00 && payload[i + 1] == 0x02) {
                rt_kprintf("RFID: Found NO.2 tag at payload[%d,%d]\n", i, i+1);
                return RFID_RESULT_ITEM_2;
            }
        }

        rt_kprintf("RFID: No matching tag ID found in payload\n");
        return RFID_RESULT_UNKNOWN;
    }

    /* 检查帧的第15和16位（索引15和16） */
    uint8_t tag_id_byte1 = data[15];  /* 第15位（从0开始计数） */
    uint8_t tag_id_byte2 = data[16];  /* 第16位（从0开始计数） */

    rt_kprintf("RFID: Tag ID at position 15-16 (bytes 15-16): 0x%02X 0x%02X\n",
               tag_id_byte1, tag_id_byte2);

    /* 识别NO.1和NO.2标签 */
    if (tag_id_byte1 == 0x00 && tag_id_byte2 == 0x01) {
        rt_kprintf("RFID: Detected NO.1 tag (0x00 0x01) at data[15-16]\n");
        return RFID_RESULT_ITEM_1;
    } else if (tag_id_byte1 == 0x00 && tag_id_byte2 == 0x02) {
        rt_kprintf("RFID: Detected NO.2 tag (0x00 0x02) at data[15-16]\n");
        return RFID_RESULT_ITEM_2;
    } else {
        /* 如果第15-16位不是预期的NO.1或NO.2，尝试在payload中搜索 */
        rt_kprintf("RFID: Position 15-16 not NO.1/NO.2. Searching in payload...\n");

        /* 打印payload数据用于调试 */
        rt_kprintf("RFID: Payload data (%d bytes): ", data_len);
        for (int i = 0; i < data_len && i < 20; i++) {
            rt_kprintf("%02X ", payload[i]);
        }
        rt_kprintf("\n");

        /* 在payload中搜索NO.1或NO.2 */
        for (int i = 0; i < data_len - 1; i++) {
            if (payload[i] == 0x00 && payload[i + 1] == 0x01) {
                rt_kprintf("RFID: Found NO.1 tag at payload[%d,%d]\n", i, i+1);
                return RFID_RESULT_ITEM_1;
            } else if (payload[i] == 0x00 && payload[i + 1] == 0x02) {
                rt_kprintf("RFID: Found NO.2 tag at payload[%d,%d]\n", i, i+1);
                return RFID_RESULT_ITEM_2;
            }
        }

        rt_kprintf("RFID: Unknown tag ID at position 15-16: 0x%02X 0x%02X\n",
                  tag_id_byte1, tag_id_byte2);
        return RFID_RESULT_UNKNOWN;
    }
}

/* 解析RFID响应（主解析函数） */
static rfid_result_t rfid_parse_response_impl(const uint8_t *data, uint8_t len)
{
    /* 1. 查找有效帧起始位置 */
    int frame_start = find_valid_frame(data, len);
    if (frame_start < 0) {
        rt_kprintf("RFID: No valid frame header (0x52 0x46) found in received data\n");
        rt_kprintf("RFID: First 16 bytes of received data: ");
        for (int i = 0; i < len && i < 16; i++) {
            rt_kprintf("%02X ", data[i]);
        }
        rt_kprintf("\n");
        return RFID_RESULT_ERROR;
    }

    /* 调整到帧起始位置 */
    data += frame_start;
    len -= frame_start;

    rt_kprintf("RFID: Found frame at offset %d, remaining length: %d\n", frame_start, len);

    /* 2. 检查基本长度 */
    if (len < 8) {
        rt_kprintf("RFID: Frame too short: %d bytes (need at least 8)\n", len);
        return RFID_RESULT_ERROR;
    }

    /* 3. 检查帧头 */
    if (data[0] != 0x52 || data[1] != 0x46) {
        rt_kprintf("RFID: Invalid frame header: 0x%02X 0x%02X\n", data[0], data[1]);
        return RFID_RESULT_ERROR;
    }

    rt_kprintf("RFID: Frame header OK: 0x%02X 0x%02X\n", data[0], data[1]);

    /* 4. 检查帧类型 */
    uint8_t frame_type = data[2];
    if (frame_type != 0x01) {
        rt_kprintf("RFID: Not response frame: 0x%02X (expected 0x01)\n", frame_type);
        return RFID_RESULT_ERROR;
    }

    rt_kprintf("RFID: Frame type OK: 0x%02X (response)\n", frame_type);

    /* 5. 检查命令码 */
    uint8_t cmd_code = data[5];
    rt_kprintf("RFID: Command code: 0x%02X\n", cmd_code);

    /* 6. 获取数据长度 */
    uint16_t data_len = (data[6] << 8) | data[7];
    rt_kprintf("RFID: Data length: %d bytes (0x%04X)\n", data_len, data_len);

    /* 7. 计算完整帧长度 */
    uint16_t total_len = 8 + data_len;
    rt_kprintf("RFID: Expected total frame length: %d bytes\n", total_len);

    if (len < total_len) {
        rt_kprintf("RFID: Incomplete frame: need %d, got %d\n", total_len, len);
        return RFID_RESULT_ERROR;
    }

    rt_kprintf("RFID: Complete frame received: %d bytes\n", total_len);

    /* 打印完整帧数据用于调试 */
    rt_kprintf("RFID: Complete frame data: ");
    for (int i = 0; i < total_len && i < 32; i++) {
        rt_kprintf("%02X ", data[i]);
    }
    rt_kprintf("\n");

    /* 8. 根据命令码解析不同响应 */
    switch (cmd_code) {
        case 0x21:  /* 开始盘存命令响应 */
            rt_kprintf("RFID: Start inventory response (0x21)\n");
            /* 开始命令通常不返回标签数据，只返回状态 */
            if (data_len >= 3) {
                uint8_t status1 = data[8];
                uint8_t status2 = data[9];
                uint8_t status3 = data[10];
                rt_kprintf("RFID: Status: 0x%02X 0x%02X 0x%02X\n", status1, status2, status3);
                if (status1 == 0x07 && status2 == 0x01 && status3 == 0x00) {
                    return RFID_RESULT_NONE;  /* 无标签 */
                }
            }
            return RFID_RESULT_NONE;

        case 0x22:  /* 主动盘存命令响应 */
            rt_kprintf("RFID: Active inventory response (0x22)\n");
            return rfid_parse_inventory_response(data, total_len);

        default:
            rt_kprintf("RFID: Unknown command code: 0x%02X\n", cmd_code);
            return RFID_RESULT_ERROR;
    }
}

/* 单次RFID扫描 - 两阶段接收版本 */
rfid_result_t rfid_scan_once(void)
{
    rt_kprintf("\n=== RFID Scan Start ===\n");

    /* 清空接收缓冲区 */
    memset(rx_buf, 0, RX_BUF_SIZE);
    int total_received = 0;

    /* 1. 清空串口接收缓冲区 */
    uint8_t dummy[128];
    int cleared = 0;
    while (rt_device_read(rs485_serial, 0, dummy, sizeof(dummy)) > 0) {
        cleared++;
        rt_thread_mdelay(1);
    }
    if (cleared > 0) {
        rt_kprintf("RFID: Cleared %d bytes from buffer\n", cleared);
    }

    /* 2. 发送主动盘存命令 */
    rt_kprintf("RFID: Sending command (%d bytes): ", CMD_FRAME_SIZE);
    for (int i = 0; i < CMD_FRAME_SIZE; i++) {
        rt_kprintf("%02X ", RFID_ACTIVE_INVENTORY_CMD[i]);
    }
    rt_kprintf("\n");

    /* 3. 使用驱动发送命令 */
    int ret = rs485_send_data((char *)RFID_ACTIVE_INVENTORY_CMD, CMD_FRAME_SIZE);
    if (ret != RT_EOK) {
        rt_kprintf("RFID: Send command failed\n");
        return RFID_RESULT_ERROR;
    }

    /* 4. 阶段1：等待ACK（0x46）或直接等待响应 */
    rt_kprintf("RFID: Phase 1 - Waiting for initial response (50ms)...\n");
    rt_tick_t start_tick = rt_tick_get();
    int ack_received = 0;

    while (rt_tick_get() - start_tick < rt_tick_from_millisecond(50)) {
        int bytes = rt_device_read(rs485_serial, 0, rx_buf + total_received, 1);
        if (bytes > 0) {
            total_received += bytes;
            if (rx_buf[0] == 0x46) {
                rt_kprintf("RFID: Received ACK: 0x%02X\n", rx_buf[0]);
                ack_received = 1;
            }
            break;  /* 收到第一个字节就退出第一阶段 */
        }
        rt_thread_mdelay(1);
    }

    /* 5. 阶段2：等待完整响应（超时1秒） */
    rt_kprintf("RFID: Phase 2 - Waiting for complete frame (timeout: 1000ms)...\n");
    start_tick = rt_tick_get();
    rt_tick_t last_data_tick = start_tick;
    int frame_complete = 0;

    while (rt_tick_get() - start_tick < rt_tick_from_millisecond(1000)) {
        /* 读取数据 */
        int bytes = rt_device_read(rs485_serial, 0, rx_buf + total_received,
                                  RX_BUF_SIZE - total_received - 1);

        if (bytes > 0) {
            total_received += bytes;
            last_data_tick = rt_tick_get();

            rt_kprintf("RFID: Received %d bytes, total: %d\n", bytes, total_received);

            /* 检查是否收到完整帧 */
            if (total_received >= 8) {
                int frame_start = find_valid_frame(rx_buf, total_received);
                if (frame_start >= 0) {
                    /* 计算数据长度 */
                    uint16_t data_len = (rx_buf[frame_start + 6] << 8) | rx_buf[frame_start + 7];
                    uint16_t total_len = 8 + data_len;

                    if (total_received - frame_start >= total_len) {
                        rt_kprintf("RFID: Complete frame detected\n");
                        frame_complete = 1;
                        break;
                    } else {
                        /* 帧不完整，继续等待 */
                        rt_kprintf("RFID: Incomplete frame: have %d, need %d\n",
                                  total_received - frame_start, total_len);
                    }
                }
            }
        } else {
            /* 如果没有收到数据超过20ms，且已经有数据，则尝试解析 */
            if (total_received > 0 &&
                rt_tick_get() - last_data_tick > rt_tick_from_millisecond(20)) {
                rt_kprintf("RFID: No data for 20ms, trying to parse...\n");
                break;
            }
        }

        rt_thread_mdelay(1);
    }

    /* 6. 检查接收结果 */
    if (total_received <= 0) {
        rt_kprintf("RFID: No data received\n");
        return RFID_RESULT_ERROR;
    }

    rt_kprintf("RFID: Total received %d bytes: ", total_received);
    for (int i = 0; i < total_received && i < 64; i++) {
        rt_kprintf("%02X ", rx_buf[i]);
    }
    if (total_received > 64) {
        rt_kprintf("... (and %d more bytes)", total_received - 64);
    }
    rt_kprintf("\n");

    /* 7. 解析响应 */
    rfid_result_t result = rfid_parse_response_impl(rx_buf, total_received);

    rt_kprintf("=== RFID Scan End: %s ===\n", rfid_get_item_name(result));

    /* 新增：触发回调函数 */
    trigger_rfid_callback(result);

    return result;
}

/* 快速RFID扫描（简化版，适合循环调用） */
rfid_result_t rfid_scan_quick(void)
{
    /* 清空接收缓冲区 */
    memset(rx_buf, 0, RX_BUF_SIZE);

    /* 清空串口缓冲区 */
    uint8_t dummy[128];
    while (rt_device_read(rs485_serial, 0, dummy, sizeof(dummy)) > 0) {
        rt_thread_mdelay(1);
    }

    /* 发送命令 */
    rs485_send_data((char *)RFID_ACTIVE_INVENTORY_CMD, CMD_FRAME_SIZE);

    /* 等待并读取响应 */
    rt_tick_t start = rt_tick_get();
    int received = 0;

    while (rt_tick_get() - start < rt_tick_from_millisecond(300)) {
        int bytes = rt_device_read(rs485_serial, 0, rx_buf + received, RX_BUF_SIZE - received - 1);
        if (bytes > 0) {
            received += bytes;

            /* 如果已经有足够数据，尝试查找帧 */
            if (received >= 8) {
                int frame_start = find_valid_frame(rx_buf, received);
                if (frame_start >= 0) {
                    /* 找到有效帧，尝试解析 */
                    uint16_t data_len = (rx_buf[frame_start + 6] << 8) | rx_buf[frame_start + 7];
                    if (received - frame_start >= 8 + data_len) {
                        /* 有完整帧，解析并返回结果 */
                        rfid_result_t result = rfid_parse_response_impl(rx_buf + frame_start, received - frame_start);
                        /* 新增：触发回调函数 */
                        trigger_rfid_callback(result);
                        return result;
                    }
                }
            }
        }
        rt_thread_mdelay(1);
    }

    rfid_result_t result = RFID_RESULT_ERROR;
    /* 新增：触发回调函数（即使是错误结果也触发） */
    trigger_rfid_callback(result);
    return result;
}

/* 获取物品名称 */
const char* rfid_get_item_name(rfid_result_t result)
{
    switch (result) {
        case RFID_RESULT_ITEM_1: return "Dangerous Goods 1";
        case RFID_RESULT_ITEM_2: return "Dangerous Goods 2";
        case RFID_RESULT_UNKNOWN: return "Unknown Item";
        case RFID_RESULT_ERROR: return "RFID Error";
        case RFID_RESULT_NONE: return "No Item";
        default: return "No Item";
    }
}

/* 获取物品ID */
const char* rfid_get_item_id(rfid_result_t result)
{
    switch (result) {
        case RFID_RESULT_ITEM_1: return "NO.1";
        case RFID_RESULT_ITEM_2: return "NO.2";
        case RFID_RESULT_UNKNOWN: return "UNKNOWN";
        case RFID_RESULT_ERROR: return "ERROR";
        case RFID_RESULT_NONE: return "NONE";
        default: return "NONE";
    }
}

/* 测试函数 - 导出到MSH命令 */
#include <finsh.h>
static void test_rfid(int argc, char *argv[])
{
    rt_err_t ret = rfid_module_init();
    if (ret != RT_EOK) {
        rt_kprintf("RFID init failed!\n");
        return;
    }

    rt_kprintf("Testing RFID module...\n");
    rfid_result_t result = rfid_scan_once();
    rt_kprintf("Result: %s (%s)\n",
               rfid_get_item_name(result),
               rfid_get_item_id(result));
}
MSH_CMD_EXPORT(test_rfid, Test RFID module);

/* 连续扫描测试 */
static void rfid_continuous_scan(int argc, char *argv[])
{
    int count = 10;
    if (argc > 1) {
        count = atoi(argv[1]);
    }

    rt_err_t ret = rfid_module_init();
    if (ret != RT_EOK) {
        rt_kprintf("RFID init failed!\n");
        return;
    }

    rt_kprintf("Starting continuous RFID scan (%d times)...\n", count);

    for (int i = 0; i < count; i++) {
        rt_kprintf("\n--- Scan %d/%d ---\n", i+1, count);
        rfid_result_t result = rfid_scan_once();
        rt_kprintf("Result: %s\n", rfid_get_item_name(result));

        if (i < count - 1) {
            rt_thread_mdelay(1000);  /* 等待1秒再进行下一次扫描 */
        }
    }
}
MSH_CMD_EXPORT(rfid_continuous_scan, Continuous RFID scan: rfid_continuous_scan [count]);

/* 发送开始盘存命令（用于调试） */
static void send_start_inventory(int argc, char *argv[])
{
    static const uint8_t START_INVENTORY_CMD[] = {
        0x52, 0x46, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x47
    };

    rt_kprintf("Sending start inventory command: ");
    for (int i = 0; i < 9; i++) {
        rt_kprintf("%02X ", START_INVENTORY_CMD[i]);
    }
    rt_kprintf("\n");

    rs485_send_data((char *)START_INVENTORY_CMD, 9);

    rt_thread_mdelay(100);

    uint8_t buf[128];
    int received = rt_device_read(rs485_serial, 0, buf, sizeof(buf));

    rt_kprintf("Received %d bytes: ", received);
    for (int i = 0; i < received && i < 32; i++) {
        rt_kprintf("%02X ", buf[i]);
    }
    rt_kprintf("\n");
}
MSH_CMD_EXPORT(send_start_inventory, Send start inventory command for debug);
