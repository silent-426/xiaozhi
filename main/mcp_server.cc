/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>
#include <serial_driver.h>
#include "application.h"
#include "display.h"
#include "board.h"
#include <freertos/ringbuf.h>  // 引入 RingbufHandle_t 定义
#include <esp_heap_caps.h>      // 引入 heap_caps_malloc/free
#include <freertos/FreeRTOS.h>    // FreeRTOS 基础定义
#include <freertos/task.h>        // xTaskCreate、pdMS_TO_TICKS 等
#include <soc/periph_defs.h>

#include <esp_intr_alloc.h>  // 中断分配相关宏
#include <soc/uart_struct.h>    // UART2 寄存器结构定义
#include <soc/uart_reg.h>       // UART 中断状态掩码宏

#define TAG "MCP"
#define VCU_NUM UART_NUM_2     // UART 设备号
#define VCU_RX_PIN 41          // RX 引脚
#define VCU_TX_PIN 40          // TX 引脚
#define VCU_BAUD 115200        // 波特率
#define DMA_BUF_SIZE 1024      // DMA 缓冲区大小
#define DEFAULT_TOOLCALL_STACK_SIZE 6144  // 工具调用默认栈大小
#define TAG "MCP"


#define DEFAULT_TOOLCALL_STACK_SIZE 6144
#define VCU_NUM UART_NUM_2
#define VCU_RX 41
#define VCU_TX 40
#define VCU_Baud 115200

#define VCU_ID 0x16               //VCU  ID
#define MCP_ID 0x11              //MCP  ID
#define CMD_CMAP_WR 0x02        //写指令（需应答）
#define LED_INDEX 0X08         //数据索引
#define Persent_INDEX 0X20         //数据索引
#define TCU_INDEX 0X30         //数据索引
// 全局队列句柄
static QueueHandle_t s_uart_queue = nullptr;

//声明
static void UartEventTask(void* arg);

// 在 McpServer 构造时调用
void McpServer::SetupUartEvent() {
    // 1) 删除旧驱动
    uart_driver_delete(VCU_PORT);

    // 2) 安装 UART driver，并创建事件队列
    ESP_ERROR_CHECK(uart_driver_install(
        VCU_PORT,
        1024,   // RX buffer
        0,               // TX buffer
        20,              // queue length
        &s_uart_queue,
        0
    ));

    // 3) 配置串口参数
    uart_config_t cfg = {
        .baud_rate = VCU_Baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(VCU_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(VCU_PORT, 41, 40,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 4) 设置触发阈值和空闲超时
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(VCU_PORT, 1));
    ESP_ERROR_CHECK(uart_set_rx_timeout(VCU_PORT, 10));

    // 5) 创建事件处理任务
    BaseType_t res = xTaskCreate(
        ::UartEventTask,
        "uart_evt",
        4096,
        nullptr,
        tskIDLE_PRIORITY + 1,
        nullptr
    );
    configASSERT(res == pdPASS);
}

// 事件任务：从全局队列读取 UART_DATA，然后回显
static void UartEventTask(void* arg) {
    uart_event_t event;
    uint8_t buf[1024];

    while (true) {
        if (xQueueReceive(s_uart_queue, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(
                    UART_NUM_2, buf,
                    std::min<int>(event.size, 1024),
                    pdMS_TO_TICKS(100)
                );
                if (len > 0) {
                    uart_write_bytes(UART_NUM_2, (const char*)buf, len);
                    //ESP_LOGI("TAG", "UART_DATA: %s", (const char*)buf);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// static RingbufHandle_t s_ringbuf = nullptr;
// static TaskHandle_t s_parse_task = nullptr;
// static volatile size_t last_index = 0;  // 上次已处理的 DMA 缓冲区索引

// static void IRAM_ATTR uart_isr(void* arg);
// static void DataParseTask(void* arg);

// void McpServer::SetupUartEvent() {
//     // 1. 删除旧驱动
//     uart_driver_delete(VCU_NUM);

//     // 2. 安装 DMA 驱动
//     ESP_ERROR_CHECK(uart_driver_install(
//         VCU_NUM,
//         DMA_BUF_SIZE * 2,  // RX 双缓冲区
//         0,
//         0,
//         nullptr,
//         0
//     ));

//     // 3. 配置 UART 参数
//     uart_config_t cfg = {
//         .baud_rate = VCU_BAUD,
//         .data_bits = UART_DATA_8_BITS,
//         .parity    = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//         .source_clk = UART_SCLK_APB,
//     };
//     ESP_ERROR_CHECK(uart_param_config(VCU_NUM, &cfg));
//     ESP_ERROR_CHECK(uart_set_pin(VCU_NUM, VCU_TX_PIN, VCU_RX_PIN,
//                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

//     // 4. 设置半满阈值与空闲超时，并启用 RX 相关中断
//     ESP_ERROR_CHECK(uart_set_rx_full_threshold(VCU_NUM, DMA_BUF_SIZE / 2));  // 半满阈值
//     ESP_ERROR_CHECK(uart_set_rx_timeout(VCU_NUM, 10));                    // 空闲超时（单位：RTOS tick）
//     ESP_ERROR_CHECK(uart_enable_rx_intr(VCU_NUM));                        // 启用接收中断

//     // 5. 创建环形缓冲区
//     s_ringbuf = xRingbufferCreate(DMA_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
//     configASSERT(s_ringbuf);

//     // 6. 注册 UART ISR
//         // 6. 通过 esp_intr_alloc 注册 UART2 中断
//     // ETS_UART2_INTR_SOURCE 定义于 soc/periph_defs.h
//     esp_intr_alloc(ETS_UART2_INTR_SOURCE,
//                   ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
//                   uart_isr,
//                   nullptr,
//                   nullptr);

//     // 7. 创建后台解析任务（当前仅回显）
//     BaseType_t res = xTaskCreate(
//         DataParseTask,
//         "mcp_parse",
//         4096,
//         nullptr,
//         tskIDLE_PRIORITY + 2,
//         &s_parse_task
//     );
//     configASSERT(res == pdPASS);
// }

// static void IRAM_ATTR uart_isr(void* arg) {
//     // 读取并清除所有中断状态
//     uint32_t intr_st = UART2.int_st.val;
//     UART2.int_clr.val = intr_st;

//     // 获取 DMA 缓冲区剩余数据量
//     size_t remain;
//     uart_get_buffered_data_len(VCU_NUM, &remain);

//     // 1) 溢出中断（满）: 处理从 last_index 到缓冲区末尾数据
//     if (intr_st & UART_RXFIFO_OVF_INT_ST_M) {
//         size_t handle = DMA_BUF_SIZE - last_index;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, nullptr);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = 0;
//     }

//     // 2) 半满中断: 当前写指针 = DMA_BUF_SIZE - remain
//     if (intr_st & UART_RXFIFO_FULL_INT_ST_M) {
//         size_t cur = DMA_BUF_SIZE - remain;
//         size_t handle = (cur + DMA_BUF_SIZE - last_index) % DMA_BUF_SIZE;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, nullptr);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = cur;
//     }

//     // 3) 空闲超时中断: 同半满处理
//     if (intr_st & UART_RXFIFO_TOUT_INT_ST_M) {
//         size_t cur = DMA_BUF_SIZE - remain;
//         size_t handle = (cur + DMA_BUF_SIZE - last_index) % DMA_BUF_SIZE;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, nullptr);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = cur;
//     }
// }

// static void DataParseTask(void* arg) {
//     size_t len;
//     uint8_t* data;
//     for (;;) {
//         data = (uint8_t*)xRingbufferReceive(s_ringbuf, &len, pdMS_TO_TICKS(500));
//         if (data) {
//             // 当前仅回显
//             uart_write_bytes(VCU_NUM, (const char*)data, len);
//             vRingbufferReturnItem(s_ringbuf, data);
//         }
//     }
// }


// static RingbufHandle_t s_ringbuf = nullptr;
// static TaskHandle_t s_parse_task = nullptr;
// static volatile size_t last_index = 0;

// static void IRAM_ATTR uart_isr(void* arg);
// static void DataParseTask(void* arg);

// void McpServer::SetupUartEvent() {
//     // 删除旧驱动
//     uart_driver_delete(VCU_NUM);
//     // 安装 DMA 驱动
//     ESP_ERROR_CHECK(uart_driver_install(VCU_NUM, DMA_BUF_SIZE * 2, 0, 0, nullptr, 0));
//     // 配置 UART
//     uart_config_t cfg = {
//         .baud_rate = VCU_BAUD,
//         .data_bits = UART_DATA_8_BITS,
//         .parity    = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//         .source_clk = UART_SCLK_APB,
//     };
//     ESP_ERROR_CHECK(uart_param_config(VCU_NUM, &cfg));
//     ESP_ERROR_CHECK(uart_set_pin(VCU_NUM, VCU_TX_PIN, VCU_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
//     // 使能中断：半满、溢出、空闲
//     ESP_ERROR_CHECK(uart_set_rx_full_threshold(VCU_NUM, DMA_BUF_SIZE / 2));
//     ESP_ERROR_CHECK(uart_set_rx_timeout(VCU_NUM, 10));
//     ESP_ERROR_CHECK(uart_enable_intr_mask(VCU_NUM,
//         UART_RXFIFO_FULL_INT_ENA_M |
//         UART_RXFIFO_OVF_INT_ENA_M  |
//         UART_RXFIFO_TOUT_INT_ENA_M
//     ));
//     // 创建环形缓冲
//     s_ringbuf = xRingbufferCreate(DMA_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
//     configASSERT(s_ringbuf);
//     // 注册 ISR
//     esp_intr_alloc(ETS_UART2_INTR_SOURCE,
//                   ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
//                   uart_isr,
//                   nullptr,
//                   nullptr);

//     // 使能中断
// ESP_ERROR_CHECK(uart_enable_rx_intr(VCU_NUM));

//     // 创建解析任务
//     BaseType_t res = xTaskCreate(DataParseTask, "mcp_parse", 4096, nullptr, tskIDLE_PRIORITY + 2, &s_parse_task);
//     configASSERT(res == pdPASS);
// }

// static void IRAM_ATTR uart_isr(void* arg) {
//     // 读取并清除中断
//    uart_dev_t* dev = nullptr;
//     if (VCU_NUM == UART_NUM_0) dev = &UART0;
//     else if (VCU_NUM == UART_NUM_1) dev = &UART1;
//     else if (VCU_NUM == UART_NUM_2) dev = &UART2;
//     if (!dev) return;

//     uint32_t intr_st = dev->int_st.val;
//     dev->int_clr.val = intr_st;
   
//     // 获取剩余数据
//     size_t remain;
//     uart_get_buffered_data_len(VCU_NUM, &remain);
//     BaseType_t higher = pdFALSE;
//     // 溢出中断
//     if (intr_st & UART_RXFIFO_OVF_INT_ST_M) {
//         size_t handle = DMA_BUF_SIZE - last_index;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, &higher);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = 0;
//     }
//     // 半满中断
//     if (intr_st & UART_RXFIFO_FULL_INT_ST_M) {
//         size_t cur = DMA_BUF_SIZE - remain;
//         size_t handle = (cur + DMA_BUF_SIZE - last_index) % DMA_BUF_SIZE;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, &higher);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = cur;
//     }
//     // 空闲中断
//     if (intr_st & UART_RXFIFO_TOUT_INT_ST_M) {
//         size_t cur = DMA_BUF_SIZE - remain;
//         size_t handle = (cur + DMA_BUF_SIZE - last_index) % DMA_BUF_SIZE;
//         if (handle) {
//             uint8_t* buf = (uint8_t*)heap_caps_malloc(handle, MALLOC_CAP_DMA);
//             if (buf) {
//                 int rd = uart_read_bytes(VCU_NUM, buf, handle, 0);
//                 xRingbufferSendFromISR(s_ringbuf, buf, rd, &higher);
//                 heap_caps_free(buf);
//             }
//         }
//         last_index = cur;
//     }
//     if (higher) portYIELD_FROM_ISR();
// }

// static void DataParseTask(void* arg) {
//     for (;;) {
//         size_t len;
//         uint8_t* data = (uint8_t*)xRingbufferReceive(s_ringbuf, &len, pdMS_TO_TICKS(500));
//         if (data) {
//             uart_write_bytes(VCU_NUM, (const char*)data, len);
//             vRingbufferReturnItem(s_ringbuf, data);
//         }
//     }
// }

// 构造函数里启动 UART
McpServer::McpServer() {
    SetupUartEvent();
}

McpServer::~McpServer() {
    
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

    auto display = board.GetDisplay();
    if (display && !display->GetTheme().empty()) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                display->SetTheme(properties["theme"].value<std::string>().c_str());
                return true;
            });
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Take a photo and explain it. Use this tool after the user asks you to see something.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->Capture()) {
                    return "{\"success\": false, \"message\": \"Failed to capture photo\"}";
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }

   AddTool("serial.send_data",
        "通过串口发送固定格式数据帧 \n"
        "格式: [0x5A][0xA5][CMD][XOR校验][0x55]\n"
        "参数:\n"
       "  `data`: 自然语言指令（如：打开车灯/开启照明 → 统一转换为\"开灯\"）",
        PropertyList({
            Property("data", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            // 固定帧格式参数
                const uint8_t FRAME_HEADER_1 = 0x5A;  //帧头
                const uint8_t FRAME_HEADER_2 = 0xA5;  //帧头
                const uint8_t FRAME_LEN=0X02;//指令长度
                const uint8_t FRAME_SRC_ID=MCP_ID;//源ID号
                const uint8_t FRAME_DES_ID=VCU_ID;//目标ID号
                const uint8_t FRAME_CMD=CMD_CMAP_WR;//命令字
                const uint8_t FRAME_INDEX=LED_INDEX;//数据索引

                //const uint8_t FRAME_FOOTER = 0x55;  //帧尾
                //const size_t FIXED_DATA_LENGTH = 6;    //有效数据长度
                
                std::string input = properties["data"].value<std::string>();
                
                // 语义统一
                static const std::unordered_map<std::string, std::string> COMMAND_MAP = {
                    {"开灯", "开灯"}, {"打开车灯", "开灯"}, {"开启照明", "开灯"},
                    {"关灯", "关灯"}, {"关闭车灯", "关灯"}, {"熄灭灯光", "关灯"}
                };
    
                // 语义匹配
                std::string unified_cmd = input;
                ESP_LOGI(TAG,"unified_cmd1=  %s",unified_cmd.c_str());
                auto it = COMMAND_MAP.find(input);
                if (it == COMMAND_MAP.end()) {
                    // 模糊匹配
                    if (input.find("开") != std::string::npos) {
                        unified_cmd = "开灯";
                        ESP_LOGI(TAG,"unified_cmdk=  %s",unified_cmd.c_str());
                    } else if (input.find("关") != std::string::npos || input.find("关") != std::string::npos) {
                        unified_cmd = "关灯";
                        ESP_LOGI(TAG,"unified_cmdg=  %s",unified_cmd.c_str());
                    }
                } else {
                    unified_cmd = it->second;
                }
                ESP_LOGI(TAG,"unified_cmdend=  %s",unified_cmd.c_str());
                // 记录转换日志
                ESP_LOGI(TAG, "指令转换: %s → %s", input.c_str(), unified_cmd.c_str());
    
                // 根据统一指令构建数据帧
                std::vector<uint8_t> cmd_data;
                if (unified_cmd == "开灯") {
                    cmd_data = {0x00, 0x01}; // 两字节 CMD 表示开灯
                } else if (unified_cmd == "关灯") {
                    cmd_data = {0x02, 0x03}; // 两字节 CMD 表示关灯
                } else
                cmd_data = {0xFF, 0xFF};
                // 构建数据帧
                std::vector<uint8_t> frame;
                // 帧头为 0xA5 0x5A
                frame.push_back(FRAME_HEADER_1);
                frame.push_back(FRAME_HEADER_2);
                frame.push_back(FRAME_LEN);
                frame.push_back(FRAME_SRC_ID);
                frame.push_back(FRAME_DES_ID);
                frame.push_back(FRAME_CMD);
                frame.push_back(FRAME_INDEX);
                // 添加两字节 数据段（cmd_data)
                frame.insert(frame.end(), cmd_data.begin(), cmd_data.end());
                
                // 计算校验
                uint32_t checksum = 0;
                // for (size_t i = 2; i < frame.size(); ++i) { // 从帧头后开始计算校验
                //     checksum ^= frame[i];
                // }
                for(int i=2;i<frame.size(); i++){
                    checksum+=frame[i];
                }
                checksum=(uint16_t)~checksum;
                frame.push_back((uint8_t)checksum);
                frame.push_back(checksum>>8);
             
                // 帧尾
                //frame.push_back(FRAME_FOOTER);
            
                 std::string frame_str(frame.begin(), frame.end());
                     // 转换为十六进制字符串
     std::string hex_str;
    for (uint8_t byte : frame_str) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_str += buf;
    }
    if (!hex_str.empty() && hex_str.back() == ' ') {
        hex_str.pop_back(); // 移除最后一个空格
    }
            // 发送
            int sent = uart_write_bytes(VCU_NUM, frame.data(), frame.size());
            if (sent > 0) ESP_LOGI(TAG, "数据发送成功 (%d 字节): %s", sent, hex_str.c_str());
     
           else  ESP_LOGE(TAG, "数据发送失败 (%d 字节): %s", static_cast<int>(frame_str.size()), hex_str.c_str());
      
            return sent == (int)frame.size();
        });

    // tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
    // 在 serial.send_data 工具后添加新工具
    AddTool("serial.set_switch_level",
        "切换TCU档位（0-6）\n"
        "格式: [0x5A][0xA5][CMD][XOR校验][0x55]\n"
        "参数:\n"
        "  `level`: 自然语言指令（如：加档减档 → 加档提取1，减档提取0）",
        PropertyList({
            Property("level", kPropertyTypeInteger, 0, 1)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            // 使用与 serial.send_data 相同的帧格式
            const uint8_t FRAME_HEADER_1 = 0x5A;
            const uint8_t FRAME_HEADER_2 = 0xA5;
            const uint8_t FRAME_LEN = 0x02;
            const uint8_t FRAME_SRC_ID = MCP_ID;
            const uint8_t FRAME_DES_ID = VCU_ID;
            const uint8_t FRAME_CMD = CMD_CMAP_WR;
            const uint8_t FRAME_INDEX = TCU_INDEX;

            int tcu = properties["level"].value<int>();
            
            // 构建数据帧
            std::vector<uint8_t> frame = {
                FRAME_HEADER_1,
                FRAME_HEADER_2,
                FRAME_LEN,
                FRAME_SRC_ID,
                FRAME_DES_ID,
                FRAME_CMD,
                FRAME_INDEX,
                static_cast<uint8_t>(tcu), // 助力比级别
                0x00  // 预留位
            };

            // 计算校验（与原有校验方式一致）
            uint16_t checksum = 0;
            for (size_t i = 2; i < frame.size(); ++i) {
                checksum += frame[i];
            }
            checksum = ~checksum;
            frame.push_back(checksum & 0xFF);
            frame.push_back((checksum >> 8) & 0xFF);

                   std::string frame_str(frame.begin(), frame.end());
                     // 转换为十六进制字符串
     std::string hex_str;
    for (uint8_t byte : frame_str) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_str += buf;
    }
    if (!hex_str.empty() && hex_str.back() == ' ') {
        hex_str.pop_back(); // 移除最后一个空格
    }
            // 发送数据
            int sent = uart_write_bytes(VCU_NUM, frame.data(), frame.size());
            if (sent > 0) {
                 ESP_LOGI(TAG, "数据发送成功 (%d 字节): %s", sent, hex_str.c_str());
                //ESP_LOGI(TAG, "助力比级别 %d 发送成功", level);
                return true;
            }
            ESP_LOGE(TAG, "档位调节发送失败");
            return false;
        });
        AddTool("serial.set_assist_level",
        "设置骑行助力比级别（0-6）\n"
        "格式: [0x5A][0xA5][CMD][XOR校验][0x55]\n"
        "参数:\n"
        "  `level`: 自然语言指令（如：设置助力比为3级 → 提取数字0-6）",
        PropertyList({
            Property("level", kPropertyTypeInteger, 0, 6)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            // 使用与 serial.send_data 相同的帧格式
            const uint8_t FRAME_HEADER_1 = 0x5A;
            const uint8_t FRAME_HEADER_2 = 0xA5;
            const uint8_t FRAME_LEN = 0x02;
            const uint8_t FRAME_SRC_ID = MCP_ID;
            const uint8_t FRAME_DES_ID = VCU_ID;
            const uint8_t FRAME_CMD = CMD_CMAP_WR;
            const uint8_t FRAME_INDEX = Persent_INDEX;

            int level = properties["level"].value<int>();
            
            // 构建数据帧
            std::vector<uint8_t> frame = {
                FRAME_HEADER_1,
                FRAME_HEADER_2,
                FRAME_LEN,
                FRAME_SRC_ID,
                FRAME_DES_ID,
                FRAME_CMD,
                FRAME_INDEX,
                static_cast<uint8_t>(level), // 助力比级别
                0x00  // 预留位
            };

            // 计算校验（与原有校验方式一致）
            uint16_t checksum = 0;
            for (size_t i = 2; i < frame.size(); ++i) {
                checksum += frame[i];
            }
            checksum = ~checksum;
            frame.push_back(checksum & 0xFF);
            frame.push_back((checksum >> 8) & 0xFF);

                   std::string frame_str(frame.begin(), frame.end());
                     // 转换为十六进制字符串
     std::string hex_str;
    for (uint8_t byte : frame_str) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_str += buf;
    }
    if (!hex_str.empty() && hex_str.back() == ' ') {
        hex_str.pop_back(); // 移除最后一个空格
    }
            // 发送数据
            int sent = uart_write_bytes(VCU_NUM, frame.data(), frame.size());
            if (sent > 0) {
                 ESP_LOGI(TAG, "数据发送成功 (%d 字节): %s", sent, hex_str.c_str());
                //ESP_LOGI(TAG, "助力比级别 %d 发送成功", level);
                return true;
            }
            ESP_LOGE(TAG, "助力比发送失败");
            return false;
        });
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
        }
        GetToolsList(id_int, cursor_str);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        auto stack_size = cJSON_GetObjectItem(params, "stackSize");
        if (stack_size != nullptr && !cJSON_IsNumber(stack_size)) {
            ESP_LOGE(TAG, "tools/call: Invalid stackSize");
            ReplyError(id_int, "Invalid stackSize");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments, stack_size ? stack_size->valueint : DEFAULT_TOOLCALL_STACK_SIZE);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;//追加工具描述
        ++it;//下一个工具
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, int stack_size) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Start a task to receive data with stack size
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.thread_name = "tool_call";
    cfg.stack_size = stack_size;
    cfg.prio = 1;
    esp_pthread_set_cfg(&cfg);

    // Use a thread to call the tool to avoid blocking the main thread
    tool_call_thread_ = std::thread([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
    tool_call_thread_.detach();
}