#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"

#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "Lamp"

namespace iot {

// 这里仅定义 Lamp_green 的属性和方法，不包含具体的实现
class Lamp_green : public Thing {
private:
#ifdef CONFIG_IDF_TARGET_ESP32
    gpio_num_t gpio_num_ = GPIO_NUM_12;
#else
    gpio_num_t gpio_num_ = GPIO_NUM_18;
#endif
    bool power_green = false;

    void InitializeGpio() {
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << gpio_num_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_num_, 0);
    }

public:
    Lamp_green() : Thing("Lamp_green", "A test Lamp_green"), power_green(false) {
        InitializeGpio();

        // 定义设备的属性
        properties_.AddBooleanProperty("power", "Whether the lamp is on", [this]() -> bool {
            return power_green;
        });

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("turn_on", "Turn on the Lamp_green", ParameterList(), [this](const ParameterList& parameters) {
            power_green = true;
            gpio_set_level(gpio_num_, 1);
        });

        methods_.AddMethod("turn_off", "Turn off the Lamp_green", ParameterList(), [this](const ParameterList& parameters) {
            power_green = false;
            gpio_set_level(gpio_num_, 0);
        });
    }
};

} // namespace iot

DECLARE_THING(Lamp_green);
