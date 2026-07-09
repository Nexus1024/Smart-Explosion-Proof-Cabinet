#include "msg.h"
#include "onenet.h"  // 添加 OneNET 头文件
#include <finsh.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 全局变量 */
static msg_data_t current_msg = {0};
static rt_bool_t msg_initialized = RT_FALSE;
static rt_bool_t finger_received = RT_FALSE;
static rt_bool_t rfid_received = RT_FALSE;
static uint8_t last_finger_operation = 0xFF;  // 记录最近一次指纹操作类型
static rt_tick_t last_finger_time = 0;         // 记录最近一次指纹操作时间

/* 静态函数声明 */
static void msg_generate_and_send(void);
static void msg_clear_flags(void);

/* 指纹回调函数 */
static void finger_callback_handler(finger_operation_t *op_data)
{
    if (!msg_initialized || op_data == RT_NULL) {
        return;
    }

    rt_kprintf("\n=== Finger Callback Triggered ===\n");
    rt_kprintf("Finger ID: 0x%04X\n", op_data->finger_id);
    rt_kprintf("Operation Type: %s\n", op_data->operation_type == 0 ? "Store" : "Take");

    /* 记录指纹信息 */
    current_msg.finger_id = op_data->finger_id;
    current_msg.operation_type = op_data->operation_type;

    /* 根据操作类型设置消息类型 */
    if (op_data->operation_type == 0) {
        current_msg.msg_type = MSG_TYPE_STORE;  // 存物
    } else {
        current_msg.msg_type = MSG_TYPE_TAKE;   // 取物
    }

    /* 记录最近的指纹操作信息，用于RFID扫描匹配 */
    last_finger_operation = op_data->operation_type;
    last_finger_time = rt_tick_get();

    finger_received = RT_TRUE;

    rt_kprintf("Finger callback processed\n");

    /* 检查是否需要立即生成消息 */
    if (rfid_received) {
        msg_generate_and_send();
    }
}

/* RFID回调函数 */
static void rfid_callback_handler(rfid_result_t result)
{
    if (!msg_initialized) {
        return;
    }

    rt_kprintf("\n=== RFID Callback Triggered ===\n");
    rt_kprintf("RFID Result: %d (%s)\n", result, rfid_get_item_name(result));

    /* 记录RFID结果 */
    current_msg.rfid_result = result;

    /* 先检查finger_received标志 */
    if (finger_received) {
        /* 有指纹数据，使用指纹回调中设置的finger_id和operation_type */
        rt_kprintf("[MSG] Using fingerprint data from callback\n");
        rt_kprintf("  Finger ID: 0x%04X\n", current_msg.finger_id);
        rt_kprintf("  Operation Type: %s (%d)\n",
                   current_msg.operation_type == 0 ? "Store" :
                   current_msg.operation_type == 1 ? "Take" : "None",
                   current_msg.operation_type);

        /* 消息类型已经在指纹回调中设置，无需修改 */
    } else if (last_finger_operation != 0xFF &&
               (rt_tick_get() - last_finger_time) < rt_tick_from_millisecond(5000)) {
        /* 5秒内有指纹操作记录，但finger_received标志被清除了 */
        rt_kprintf("[MSG] Using last fingerprint operation (within 5 seconds)\n");
        if (last_finger_operation == 0) {
            current_msg.msg_type = MSG_TYPE_STORE;  // 存物
        } else {
            current_msg.msg_type = MSG_TYPE_TAKE;   // 取物
        }
        current_msg.operation_type = last_finger_operation;
        current_msg.finger_id = 0x00;  /* 指纹ID固定为0x00 */
    } else {
        /* 单纯的RFID扫描 */
        rt_kprintf("[MSG] No fingerprint data, treating as pure scan\n");
        current_msg.msg_type = MSG_TYPE_SCAN;
        /* 注意：这里不重置finger_id和operation_type，因为它们可能被之前的操作设置过 */
    }

    rfid_received = RT_TRUE;

    rt_kprintf("RFID callback processed\n");

    /* 检查是否需要立即生成消息 */
    if (finger_received) {
        msg_generate_and_send();
    } else {
        /* 如果没有指纹操作，也直接生成消息（单纯RFID扫描） */
        msg_generate_and_send();
    }
}

/* 生成并发送消息 */
static void msg_generate_and_send(void)
{
    if (!msg_initialized) {
        return;
    }

    rt_kprintf("\n[MSG] Generating and sending message:\n");
    rt_kprintf("----------------------------------------\n");

    /* 检查当前数据 */
    rt_kprintf("[MSG] Current message data:\n");
    rt_kprintf("  Message Type: %s\n", msg_type_to_str(current_msg.msg_type));
    rt_kprintf("  Finger ID: 0x%04X\n", current_msg.finger_id);
    rt_kprintf("  Operation Type: %s (%d)\n",
               current_msg.operation_type == 0 ? "Store" :
               current_msg.operation_type == 1 ? "Take" : "None",
               current_msg.operation_type);
    rt_kprintf("  RFID Result: %s\n", rfid_get_item_name(current_msg.rfid_result));

    /* 生成JSON格式消息（用于串口输出调试） */
    char json_buf[256] = {0};
    const char* rfid_item_name = rfid_get_item_name(current_msg.rfid_result);
    const char* rfid_item_id = rfid_get_item_id(current_msg.rfid_result);

    /* 根据消息类型构建不同的JSON消息 */
    if (current_msg.msg_type == MSG_TYPE_SCAN) {
        /* 只有RFID扫描 */
        rt_snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"type\": \"scan\","
            "\"rfid\": {"
            "\"item_name\": \"%s\","
            "\"item_id\": \"%s\""
            "}"
            "}",
            rfid_item_name, rfid_item_id);
    } else if (current_msg.msg_type == MSG_TYPE_STORE ||
               current_msg.msg_type == MSG_TYPE_TAKE) {
        /* 存物或取物操作（包含指纹和RFID） */
        const char* operation_str = (current_msg.operation_type == 0) ? "store" : "take";

        /* 检查finger_id是否有效 */
        if (current_msg.finger_id == 0xFFFF) {
            rt_kprintf("[WARNING] Invalid finger_id (0xFFFF) in store/take message\n");
            current_msg.finger_id = 0x00;  // 使用默认值
        }

        rt_snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"type\": \"%s\","
            "\"finger\": {"
            "\"finger_id\": %d,"
            "\"operation\": \"%s\""
            "},"
            "\"rfid\": {"
            "\"item_name\": \"%s\","
            "\"item_id\": \"%s\""
            "}"
            "}",
            operation_str,
            current_msg.finger_id, operation_str,
            rfid_item_name, rfid_item_id);

        /* 上传到OneNET（仅对存物/取物操作） */
        rt_kprintf("[MSG] Uploading to OneNET...\n");
        int ret = onenet_upload_event(&current_msg);
        if (ret == 0) {
            rt_kprintf("[MSG] OneNET upload successful\n");
        } else {
            rt_kprintf("[MSG] OneNET upload failed (code: %d)\n", ret);
        }
    } else {
        /* 未知消息类型 */
        rt_snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"type\": \"unknown\","
            "\"error\": \"invalid_message_type\""
            "}");
    }

    /* 通过串口输出消息（用于调试） */
    rt_kprintf("%s\n", json_buf);
    rt_kprintf("----------------------------------------\n");

    /* 输出调试信息 */
    rt_kprintf("[MSG] Message Details:\n");
    rt_kprintf("  Message Type: %s\n", msg_type_to_str(current_msg.msg_type));
    rt_kprintf("  Finger ID: 0x%04X\n", current_msg.finger_id);
    rt_kprintf("  Operation Type: %s (%d)\n",
               current_msg.operation_type == 0 ? "Store" :
               current_msg.operation_type == 1 ? "Take" : "None",
               current_msg.operation_type);
    rt_kprintf("  RFID Result: %s (%s)\n", rfid_item_name, rfid_item_id);
    rt_kprintf("----------------------------------------\n");

    /* 清空标志位，准备接收下一条消息 */
    msg_clear_flags();
}

/* 清空标志位 */
static void msg_clear_flags(void)
{
    finger_received = RT_FALSE;
    rfid_received = RT_FALSE;

    /* 注意：这里不清空current_msg，但重置消息类型为NONE */
    current_msg.msg_type = MSG_TYPE_NONE;
}

/* 消息模块初始化 */
rt_err_t msg_module_init(void)
{
    if (msg_initialized) {
        rt_kprintf("[MSG] Module already initialized\n");
        return RT_EOK;
    }

    rt_kprintf("\n[MSG] Initializing message module...\n");

    /* 初始化变量 */
    memset(&current_msg, 0, sizeof(current_msg));
    current_msg.msg_type = MSG_TYPE_NONE;
    finger_received = RT_FALSE;
    rfid_received = RT_FALSE;
    last_finger_operation = 0xFF;
    last_finger_time = 0;

    /* 注册指纹回调 */
    finger_register_callback(finger_callback_handler);
    rt_kprintf("[MSG] Registered finger callback\n");

    /* 注册RFID回调 */
    rfid_register_callback(rfid_callback_handler);
    rt_kprintf("[MSG] Registered RFID callback\n");

    msg_initialized = RT_TRUE;

    rt_kprintf("[MSG] Message module initialized successfully\n");
    rt_kprintf("[MSG] Ready to process finger and RFID events\n");
    rt_kprintf("[MSG] Messages will be output via UART in JSON format\n");
    rt_kprintf("[MSG] Store/Take operations will be uploaded to OneNET\n");

    return RT_EOK;
}

/* 消息类型转字符串 */
const char* msg_type_to_str(msg_type_t type)
{
    switch (type) {
        case MSG_TYPE_NONE:   return "NONE";
        case MSG_TYPE_STORE:  return "STORE";
        case MSG_TYPE_TAKE:   return "TAKE";
        case MSG_TYPE_SCAN:   return "SCAN";
        default:              return "UNKNOWN";
    }
}

/* 手动发送测试消息（用于调试） */
static void msg_send_test(int argc, char *argv[])
{
    if (!msg_initialized) {
        rt_kprintf("[MSG] Module not initialized. Run 'msg_init' first.\n");
        return;
    }

    msg_data_t test_msg = {0};

    /* 设置测试消息 */
    test_msg.msg_type = MSG_TYPE_STORE;
    test_msg.finger_id = 0x00;
    test_msg.operation_type = 0;  /* 存物 */
    test_msg.rfid_result = RFID_RESULT_ITEM_1;

    /* 发送测试消息 */
    msg_process_and_send(&test_msg);
}
MSH_CMD_EXPORT(msg_send_test, Send a test message: msg_send_test);

/* 初始化消息模块（MSH命令） */
static void msg_init_cmd(int argc, char *argv[])
{
    rt_err_t ret = msg_module_init();
    if (ret == RT_EOK) {
        rt_kprintf("[MSG] Initialization successful\n");
    } else {
        rt_kprintf("[MSG] Initialization failed: %d\n", ret);
    }
}
MSH_CMD_EXPORT(msg_init_cmd, Initialize message module: msg_init_cmd);

/* 手动触发指纹回调（用于测试） */
static void msg_test_finger(int argc, char *argv[])
{
    if (!msg_initialized) {
        rt_kprintf("[MSG] Module not initialized. Run 'msg_init' first.\n");
        return;
    }

    finger_operation_t op = {0};
    op.finger_id = 0x00;

    /* 如果没有参数，默认存物操作 */
    if (argc == 1) {
        op.operation_type = 0;  /* 存物 */
    } else {
        int op_type = atoi(argv[1]);
        if (op_type == 0 || op_type == 1) {
            op.operation_type = op_type;
        } else {
            rt_kprintf("[MSG] Invalid operation type. Use 0 for store, 1 for take.\n");
            return;
        }
    }

    rt_kprintf("[MSG] Manually triggering finger callback:\n");
    rt_kprintf("  Finger ID: 0x%04X\n", op.finger_id);
    rt_kprintf("  Operation: %s\n", op.operation_type == 0 ? "Store" : "Take");

    finger_callback_handler(&op);
}
MSH_CMD_EXPORT(msg_test_finger, Manually trigger finger callback: msg_test_finger [0=store|1=take]);

/* 手动触发RFID回调（用于测试） */
static void msg_test_rfid(int argc, char *argv[])
{
    if (!msg_initialized) {
        rt_kprintf("[MSG] Module not initialized. Run 'msg_init' first.\n");
        return;
    }

    rfid_result_t result = RFID_RESULT_ITEM_1;

    /* 如果没有参数，默认ITEM_1 */
    if (argc > 1) {
        int item = atoi(argv[1]);
        switch (item) {
            case 1: result = RFID_RESULT_ITEM_1; break;
            case 2: result = RFID_RESULT_ITEM_2; break;
            case 3: result = RFID_RESULT_UNKNOWN; break;
            case 4: result = RFID_RESULT_ERROR; break;
            case 5: result = RFID_RESULT_NONE; break;
            default: rt_kprintf("[MSG] Invalid item. Use 1-5.\n"); return;
        }
    }

    rt_kprintf("[MSG] Manually triggering RFID callback:\n");
    rt_kprintf("  Result: %s (%s)\n",
               rfid_get_item_name(result), rfid_get_item_id(result));

    rfid_callback_handler(result);
}
MSH_CMD_EXPORT(msg_test_rfid, Manually trigger RFID callback: msg_test_rfid [1-5]);

/* 显示当前消息状态 */
static void msg_status(int argc, char *argv[])
{
    rt_kprintf("\n[MSG] Current Message Module Status:\n");
    rt_kprintf("----------------------------------------\n");
    rt_kprintf("Initialized: %s\n", msg_initialized ? "Yes" : "No");
    rt_kprintf("Finger Received: %s\n", finger_received ? "Yes" : "No");
    rt_kprintf("RFID Received: %s\n", rfid_received ? "Yes" : "No");
    rt_kprintf("Last Finger Operation: ");
    if (last_finger_operation == 0xFF) {
        rt_kprintf("None\n");
    } else {
        rt_kprintf("%s (%d)\n",
                   last_finger_operation == 0 ? "Store" : "Take",
                   last_finger_operation);
    }
    rt_kprintf("Last Finger Time: %u ticks ago\n",
               rt_tick_get() - last_finger_time);

    rt_kprintf("\nCurrent Message Buffer:\n");
    rt_kprintf("  Message Type: %s\n", msg_type_to_str(current_msg.msg_type));
    rt_kprintf("  Finger ID: 0x%04X\n", current_msg.finger_id);
    rt_kprintf("  Operation Type: %s (%d)\n",
               current_msg.operation_type == 0 ? "Store" :
               current_msg.operation_type == 1 ? "Take" : "None",
               current_msg.operation_type);
    rt_kprintf("  RFID Result: %s\n", rfid_get_item_name(current_msg.rfid_result));
    rt_kprintf("----------------------------------------\n");
}
MSH_CMD_EXPORT(msg_status, Show message module status: msg_status);

/* 清空消息缓冲区 */
static void msg_clear(int argc, char *argv[])
{
    msg_clear_flags();
    memset(&current_msg, 0, sizeof(current_msg));
    current_msg.msg_type = MSG_TYPE_NONE;
    last_finger_operation = 0xFF;
    last_finger_time = 0;

    rt_kprintf("[MSG] Message buffer cleared\n");
}
MSH_CMD_EXPORT(msg_clear, Clear message buffer: msg_clear);

/* 消息处理函数（模拟上传到OneNET，这里用串口输出） */
void msg_process_and_send(msg_data_t *msg)
{
    if (msg == RT_NULL) {
        rt_kprintf("[MSG] Error: msg is NULL\n");
        return;
    }

    /* 临时处理消息并输出 */
    rt_kprintf("\n[MSG] Processing message:\n");
    rt_kprintf("  Type: %s\n", msg_type_to_str(msg->msg_type));
    rt_kprintf("  Finger ID: 0x%04X\n", msg->finger_id);
    rt_kprintf("  Operation: %s\n", msg->operation_type == 0 ? "Store" : "Take");
    rt_kprintf("  RFID: %s\n", rfid_get_item_name(msg->rfid_result));

    /* 如果消息类型是存物或取物，上传到OneNET */
    if (msg->msg_type == MSG_TYPE_STORE || msg->msg_type == MSG_TYPE_TAKE) {
        rt_kprintf("[MSG] Uploading to OneNET...\n");
        int ret = onenet_upload_event(msg);
        if (ret == 0) {
            rt_kprintf("[MSG] OneNET upload successful\n");
        } else {
            rt_kprintf("[MSG] OneNET upload failed (code: %d)\n", ret);
        }
    } else {
        rt_kprintf("[MSG] Message type not for OneNET upload (type: %s)\n",
                   msg_type_to_str(msg->msg_type));
    }
}
