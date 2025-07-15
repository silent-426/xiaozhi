#ifndef SERIAL_DRIVER_H
#define SERIAL_DRIVER_H

#include <string>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class SerialDriver {
public:
    SerialDriver(uart_port_t uart_num, int txd_pin, int rxd_pin, int baud_rate);
    ~SerialDriver();
    bool sendData(const std::string& data);

private:
    uart_port_t uart_num_;
    // 移除接收相关成员变量声明
    // QueueHandle_t uart_queue_;
    // TaskHandle_t task_handle_;
    // static constexpr int BUF_SIZE = 128;
};

#endif // SERIAL_DRIVER_H
