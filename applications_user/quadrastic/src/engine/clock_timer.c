#include "clock_timer.h"
#include <stdlib.h>

#include <esp_timer.h>

/* Port: the STM32 TIM2 + LL driver version cannot run on ESP32.
 * esp_timer provides the same periodic-callback semantics. */
typedef struct {
    ClockTimerCallback callback;
    void* context;
} ClockTimer;

static ClockTimer clock_timer = {
    .callback = NULL,
    .context = NULL,
};

static esp_timer_handle_t clock_timer_handle = NULL;

static void clock_timer_trampoline(void* arg) {
    (void)arg;
    if(clock_timer.callback) {
        clock_timer.callback(clock_timer.context);
    }
}

void clock_timer_start(ClockTimerCallback callback, void* context, float period) {
    clock_timer_stop(); /* idempotent restart */
    clock_timer.callback = callback;
    clock_timer.context = context;

    uint64_t period_us = (uint64_t)(period * 1000000.0f);
    if(period_us == 0) period_us = 1000;

    const esp_timer_create_args_t args = {
        .callback = clock_timer_trampoline,
        .name = "quadrastic_tick",
    };
    if(esp_timer_create(&args, &clock_timer_handle) != ESP_OK) {
        clock_timer_handle = NULL;
        return;
    }
    if(esp_timer_start_periodic(clock_timer_handle, period_us) != ESP_OK) {
        esp_timer_delete(clock_timer_handle);
        clock_timer_handle = NULL;
    }
}

void clock_timer_stop(void) {
    if(clock_timer_handle) {
        esp_timer_stop(clock_timer_handle);
        esp_timer_delete(clock_timer_handle);
        clock_timer_handle = NULL;
    }
    clock_timer.callback = NULL;
    clock_timer.context = NULL;
}
