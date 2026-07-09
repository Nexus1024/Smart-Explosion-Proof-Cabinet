# 基于 RT-Spark 的危化品智能防爆柜控制系统

本项目基于 RT-Spark 星火一号开发板和 STM32F407 主控芯片，运行 RT-Thread 实时操作系统，集成 ZW101 指纹识别模块、CPH-F103 超高频 RFID 读写器、RW007 WiFi 模块、LCD 显示屏、按键和 SG90 舵机，实现危化品存取过程中的身份认证、物品识别、柜门控制和 OneNET 云端数据上传。

## 主要功能

- KEY0 取物、KEY1 存物、KEY2 短按 RFID 扫描 / 长按关门；
- 指纹验证成功后才驱动舵机开门；
- 指纹失败、超时或错误返回包不会开门，并支持重试；
- RFID 扫描物品标签，生成物品编号记录；
- 整合用户 ID、操作类型和物品 ID，通过 MQTT 上传到 OneNET；
- LCD 显示欢迎、等待指纹、验证结果、扫描提示和关门提示等状态。

## 硬件组成

- 主控平台：RT-Spark 星火一号开发板 / STM32F407
- 操作系统：RT-Thread
- 指纹模块：海凌科 ZW101，UART 通信
- RFID 模块：CPH-F103 超高频 RFID，RS485 通信
- WiFi 模块：RW007，SPI 通信
- 显示与交互：1.3 英寸 LCD、KEY0 / KEY1 / KEY2
- 执行机构：SG90 舵机，PWM 控制

## 工程说明

本仓库为比赛提交用源码工程，已删除编译产物、缓存文件、日志文件和无关大文件。为了避免公开泄露，WiFi 密码和 OneNET 鉴权信息已替换为占位符。实际使用前请在对应配置文件中填写自己的参数。

需要修改的位置：

```c
#define WLAN_SSID      "your_wifi_ssid"
#define WLAN_PASSWORD  "your_wifi_password"
```

以及 RT-Thread / OneNET 配置中的产品 ID、设备名和鉴权信息。

## 开发环境

- RT-Thread Studio
- RT-Spark / STM32F407
- RT-Thread 软件包：RW007、OneNET、Paho MQTT、cJSON

## 注意事项

公开仓库中不要提交真实 WiFi 密码、OneNET Token、设备密钥、个人账号信息、编译生成文件、演示视频和技术报告原件。
