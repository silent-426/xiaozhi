#include "serial_driver.h"
#include <esp_log.h>
#include <cstring>
#include <stdlib.h>
#define TAG "SERIAL"
#define BUF_SIZE 1024
SerialDriver::SerialDriver(uart_port_t uart_num, int txd_pin, int rxd_pin, int baud_rate) : uart_num_(uart_num) {
    // 配置串口参数
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_APB,
    };
    // 应用参数配置
    uart_param_config(uart_num_, &uart_config);
      // 设置串口引脚
    uart_set_pin(uart_num_, txd_pin, rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // 安装串口驱动
    uart_driver_install(uart_num_, BUF_SIZE, 0, 0, nullptr, 0);
}

SerialDriver::~SerialDriver() {
    // 卸载串口驱动
    uart_driver_delete(uart_num_);
    ESP_LOGI(TAG, "Serial driver deinitialized");
}

bool SerialDriver::sendData(const std::string& data) {
    // 转换为十六进制字符串
     std::string hex_str;
    for (uint8_t byte : data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_str += buf;
    }
    if (!hex_str.empty() && hex_str.back() == ' ') {
        hex_str.pop_back(); // 移除最后一个空格
    }
    int len = uart_write_bytes(uart_num_, data.data(), data.size());//发送数据帧
    if (len > 0) {
        ESP_LOGI(TAG, "数据发送成功 (%d 字节): %s", len, hex_str.c_str());
        return true;
    } else {
        ESP_LOGE(TAG, "数据发送失败 (%d 字节): %s", static_cast<int>(data.size()), hex_str.c_str());
        return false;
    }
}