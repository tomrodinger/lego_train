/**
 * @file main.c
 * @brief
 *
 * Copyright (c) 2021 Bouffalolab team
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */
#include <math.h>
#include "bflb_platform.h"
#include "bl702_ef_ctrl.h"
#include "bl702_glb.h"
#include "hal_gpio.h"
#include "hal_clock.h"
#include "hal_pm.h"
#include "hal_pwm.h"
#include "hal_wdt.h"
#include "bl702_sec_eng.h"
#include <FreeRTOS.h>
#include "task.h"
#include "ble_app.h"

#define LED_PIN     1
#define MOTOR1_PIN 24
#define MOTOR2_PIN 23

#define SHUTDOWN_TIME       5
#define THREAD_CYCLE_TIME   20
#define THREAD_IDLE_TIME    (100 / THREAD_CYCLE_TIME)

static StackType_t main_stack[2048];
static StaticTask_t main_task_handle;

extern uint8_t _heap_start;
extern uint8_t _heap_size; // @suppress("Type cannot be resolved")
extern uint8_t _heap1_start;
extern uint8_t _heap1_size; // @suppress("Type cannot be resolved")
static HeapRegion_t xHeapRegions[] = {
    { &_heap1_start, (unsigned int)&_heap1_size },
    { &_heap_start, (unsigned int)&_heap_size },
    { NULL, 0 }, /* Terminates the array. */
    { NULL, 0 }  /* Terminates the array. */
};

void user_vAssertCalled(void) __attribute__((weak, alias("vAssertCalled")));
void vAssertCalled(void)
{
    MSG("vAssertCalled\r\n");

    while (1)
        ;
}

void vApplicationTickHook(void)
{
    //MSG("vApplicationTickHook\r\n");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    MSG("vApplicationStackOverflowHook\r\n");

    if (pcTaskName) {
        MSG("Stack name %s\r\n", pcTaskName);
    }

    while (1)
        ;
}

void vApplicationMallocFailedHook(void)
{
    MSG("vApplicationMallocFailedHook\r\n");

    while (1)
        ;
}
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    /* If the buffers to be provided to the Idle task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* configSUPPORT_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize)
{
    /* If the buffers to be provided to the Timer task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    /* Pass out a pointer to the StaticTask_t structure in which the Timer
    task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

ATTR_TCM_SECTION void board_shutdown(uint32_t time)
{
    bt_le_adv_stop();
    bt_disable();

    BL_WR_REG(HBN_BASE, HBN_RSV3, 0xA5A55A5A);

    gpio_set_mode(LED_PIN, GPIO_INPUT_MODE);
    gpio_set_mode(MOTOR1_PIN, GPIO_INPUT_MODE);
    gpio_set_mode(MOTOR2_PIN, GPIO_INPUT_MODE);

    Sec_Eng_Trng_Disable();
    bflb_platform_deinit();

    pm_hbn_mode_enter(PM_HBN_LEVEL_1, time);
}

static void main_task(void *pvParameters)
{
    struct device *wdg;
    uint8_t idle_cnt = 0;

    wdg = device_find("wdg_rst");

    ble_app_init();

    gpio_set_mode(LED_PIN, GPIO_OUTPUT_PP_MODE);
    gpio_write(LED_PIN, 0);

    gpio_set_mode(MOTOR1_PIN, GPIO_OUTPUT_PP_MODE);
    gpio_write(MOTOR1_PIN, 0);

    gpio_set_mode(MOTOR2_PIN, GPIO_OUTPUT_PP_MODE);
    gpio_write(MOTOR2_PIN, 0);

    while(1) {
        if (ble_app_process()) {
            idle_cnt = 0;
        }
        
        idle_cnt++;
        if (idle_cnt >= THREAD_IDLE_TIME) {
            board_shutdown(SHUTDOWN_TIME);
        }

        if (wdg) {
            device_control(wdg, DEVICE_CTRL_RST_WDT_COUNTER, NULL);
        }

        vTaskDelay(pdMS_TO_TICKS(THREAD_CYCLE_TIME));
    }
}

int main(void)
{
    struct device *wdg;
    uint32_t tmpVal = 0;

    bflb_platform_print_set(0);
    bflb_platform_init(0);

    HBN_Set_XCLK_CLK_Sel(HBN_XCLK_CLK_XTAL);

    //Set capcode
    tmpVal = BL_RD_REG(AON_BASE, AON_XTAL_CFG);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, AON_XTAL_CAPCODE_IN_AON, 33);
    tmpVal = BL_SET_REG_BITS_VAL(tmpVal, AON_XTAL_CAPCODE_OUT_AON, 33);
    BL_WR_REG(AON_BASE, AON_XTAL_CFG, tmpVal);

    wdt_register(WDT_INDEX, "wdg_rst");
    wdg = device_find("wdg_rst");

    if (wdg) {
        uint32_t wdg_timeout = 0xFFFF;

        device_control(wdg, DEVICE_CTRL_CLR_INT, NULL);
        device_open(wdg, 0);
        device_write(wdg, 0, &wdg_timeout, sizeof(wdg_timeout));
    }

    GLB_Disable_DLL_All_Clks();
    GLB_Power_Off_DLL();

    Sec_Eng_Trng_Enable();

    vPortDefineHeapRegions(xHeapRegions);

    xTaskCreateStatic(main_task, (char *)"main", sizeof(main_stack) / 4, NULL, configMAX_PRIORITIES - 1, main_stack, &main_task_handle);

    vTaskStartScheduler();
}
