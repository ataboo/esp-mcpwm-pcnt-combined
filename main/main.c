#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "driver/pcnt.h"

static const char* TAG = "PWM_STEP_TEST";

#define TICK_PIN GPIO_NUM_18
#define DIR_PIN GPIO_NUM_19

// #define USE_LOOPBACK
#ifdef USE_LOOPBACK
    #define PCNT_PIN GPIO_NUM_5
#else
    #define PCNT_PIN TICK_PIN
#endif

#define MCPWM_UNIT    MCPWM_UNIT_0
#define MCPWM_TIMER   MCPWM_TIMER_0

#define PCNT_UNIT     PCNT_UNIT_0
#define PCNT_CHANNEL  PCNT_CHANNEL_0

static QueueHandle_t isr_queue;
static pcnt_isr_handle_t counter_isr_handle;

static void IRAM_ATTR counter_isr(void* args) {
    int8_t val = 0;
    xQueueSendFromISR(isr_queue, &val, NULL);
    PCNT.int_clr.val = 1ULL<<PCNT_UNIT;
}

static void read_task(void* args) {
    int8_t val;
    int16_t count;
    
    while(true) {
        xQueueReceive(isr_queue, &val, portMAX_DELAY);

        if (val < 0) {

            ESP_LOGI(TAG, "Read task quit signal received.");
            break;
        }
        pcnt_get_counter_value(PCNT_UNIT, &count);
        ESP_LOGI(TAG, "Got event at: %d", count);
    }

    vTaskDelete(NULL);
}

static void dump_regs(const char* message) {
    ESP_LOGI(TAG, "%s", message);
    ESP_LOGI(TAG, "GPIO en    | FUNC43     | FUNC18     | MUX 18     | MUX 19     | FUNC 45");
    ESP_LOGI(TAG, "0x%08x | 0x%08x | 0x%08x | 0x%08x | 0x%08x | 0x%08x", 
        REG_READ(GPIO_ENABLE_REG),
        REG_READ(GPIO_FUNC43_IN_SEL_CFG_REG), REG_READ(GPIO_FUNC18_OUT_SEL_CFG_REG),
        REG_READ(IO_MUX_GPIO18_REG),
        REG_READ(IO_MUX_GPIO19_REG), REG_READ(GPIO_FUNC45_IN_SEL_CFG_REG)
    );
}

static void dump_count(const char* message) {
    int16_t count;
    pcnt_get_counter_value(PCNT_UNIT, &count);
    ESP_LOGI(TAG, "[%s] | Got count: %d", message, count);
}

static void init_dir() {
    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<DIR_PIN),
        .pull_down_en = false,
        .pull_up_en = false
    };

    gpio_config(&cfg);
}

static void init_mcpwm() {
    mcpwm_config_t cfg = {
        .cmpr_a = 0,
        .cmpr_b = 0,
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
        .frequency = 500,
    };

    mcpwm_gpio_init(MCPWM_UNIT, MCPWM0A, TICK_PIN);
    mcpwm_init(MCPWM_UNIT, MCPWM_TIMER, &cfg);
}

static void init_pcnt() {
    pcnt_config_t cfg = {
        .pulse_gpio_num = PCNT_PIN,
        .ctrl_gpio_num = DIR_PIN,
        .channel = PCNT_CHANNEL,
        .unit = PCNT_UNIT,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DIS,
        .lctrl_mode = PCNT_MODE_REVERSE,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_l_lim = -10000,
        .counter_h_lim = 10000
    };

    ESP_ERROR_CHECK(pcnt_unit_config(&cfg));

    pcnt_set_event_value(PCNT_UNIT, PCNT_EVT_THRES_0, 1000);
    pcnt_set_event_value(PCNT_UNIT, PCNT_EVT_THRES_1, 500);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_L_LIM);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_ZERO);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_THRES_0);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_THRES_1);

    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);

    pcnt_isr_register(counter_isr, NULL, 0, &counter_isr_handle);
    pcnt_intr_enable(PCNT_UNIT);

    pcnt_set_filter_value(PCNT_UNIT, 1);
    pcnt_filter_enable(PCNT_UNIT);

    pcnt_counter_resume(PCNT_UNIT);

}

static void fix_regs(uint16_t pulse_signal) {
    REG_SET_BIT(GPIO_ENABLE_REG, (1ULL<<TICK_PIN|1ULL<<DIR_PIN));
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[DIR_PIN]);

#ifndef USE_LOOPBACK
    gpio_matrix_out(TICK_PIN, pulse_signal, 0, 0);
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[TICK_PIN]);
#endif
}

static void dispose() {
    int8_t quit_val = -1;
    xQueueSend(isr_queue, &quit_val, portMAX_DELAY);

    pcnt_counter_pause(PCNT_UNIT);
    pcnt_intr_disable(PCNT_UNIT);
    pcnt_isr_unregister(counter_isr_handle);
}

static void init() {
    isr_queue = xQueueCreate(10, sizeof(int8_t));
    xTaskCreatePinnedToCore(read_task, "pcnt-read-task", 2048, NULL, 1, NULL, 1);

    dump_regs("Before setup");
    
    init_dir();
    dump_regs("After dir");
    
    init_pcnt();
    dump_regs("After PCNT");

    init_mcpwm();
    dump_regs("After MCPWM");

    fix_regs(PWM0_OUT0A_IDX);
    dump_regs("After fix");
}

static void step_and_wait(bool forward, int delay_ms) {
    gpio_set_level(DIR_PIN, (uint32_t)forward);
    mcpwm_set_duty(MCPWM_UNIT, MCPWM_TIMER, MCPWM_GEN_A, 10.0f);
    mcpwm_start(MCPWM_UNIT, MCPWM_TIMER);
    vTaskDelay(3000/portTICK_PERIOD_MS);
    mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);
}

void app_main(void)
{
    init();

    step_and_wait(true, 3000);
    dump_count("3 seconds fwd");

    step_and_wait(false, 3000);
    dump_count("3 seconds back");

    step_and_wait(true, 3000);
    dump_count("3 seconds fwd");

    dispose();

    ESP_LOGI(TAG, "done.");
    vTaskDelay(portMAX_DELAY);
}
