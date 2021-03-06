/*
 * KubOS RT
 * Copyright (C) 2016 Kubos Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include "kubos-hal/gpio.h"
#include "kubos-hal/uart.h"

#include <csp/csp.h>
#include <csp/arch/csp_thread.h>
#include <csp/drivers/usart.h>
#include <csp/interfaces/csp_if_kiss.h>

#include "types.h"

/**
* Enabling this example code requires certain configuration values to be present
* in the configuration json of this application. An example is given below:
*
*  {
*      "CSP": {
*           "my_address": "1",
*           "target_address": "2",
*           "port": "10",
*           "uart_bus": "K_UART6",
*           "usart": {
*           }
*      }
*  }
*
* This would create enable CSP KISS, the address of your device and target device,
* the listening port and UART interface. Invert the addresses when flashing the target board.
*/

#define MY_ADDRESS YOTTA_CFG_CSP_MY_ADDRESS
#define TARGET_ADDRESS YOTTA_CFG_CSP_TARGET_ADDRESS
#define MY_PORT    YOTTA_CFG_CSP_PORT
#define BLINK_MS 100

/* kiss interfaces */
static csp_iface_t csp_if_kiss;
static csp_kiss_handle_t csp_kiss_driver;

static inline void blink(int pin) {
    k_gpio_write(pin, 1);
    vTaskDelay(BLINK_MS / portTICK_RATE_MS);
    k_gpio_write(pin, 0);
}

void csp_server(void *p) {
    (void) p;

    portBASE_TYPE task_woken = pdFALSE;
    /* Create socket without any socket options */
    csp_socket_t *sock = csp_socket(CSP_SO_NONE);

    /* Bind all ports to socket */
    csp_bind(sock, CSP_ANY);

    /* Create 10 connections backlog queue */
    csp_listen(sock, 10);

    /* Pointer to current connection and packet */
    csp_conn_t *conn;
    csp_packet_t *packet;

    /* Process incoming connections */
    while (1) {

        /* Wait for connection, 100 ms timeout */
        if ((conn = csp_accept(sock, 100)) == NULL)
            continue;

        /* Read packets. Timout is 100 ms */
        while ((packet = csp_read(conn, 100)) != NULL) {
            switch (csp_conn_dport(conn)) {
                case MY_PORT:
                    /* Process packet here */
                    blink(K_LED_GREEN);
                    csp_buffer_free(packet);
                    break;

                default:
                    /* Let the service handler reply pings, buffer use, etc. */
                    #ifdef TARGET_LIKE_MSP430
                    blink(K_LED_GREEN);
                    blink(K_LED_RED);
                    #else
                    blink(K_LED_BLUE);
                    #endif
                    csp_service_handler(conn, packet);
                    break;
            }
        }

        /* Close current connection, and handle next */
        csp_close(conn);

    }
}

void csp_client(void *p) {
    (void) p;
    csp_packet_t * packet;
    csp_conn_t * conn;
    portBASE_TYPE status;
    int signal;

    /**
     * Try ping
     */
    csp_sleep_ms(200);

#ifdef TARGET_LIKE_MSP430
    blink(K_LED_RED);
#else
    blink(K_LED_ORANGE);
#endif
    int result = csp_ping(TARGET_ADDRESS, 100, 100, CSP_O_NONE);
    /* if successful ping */
    if (result) {
#ifdef TARGET_LIKE_MSP430
        blink(K_LED_RED);
#else
        blink(K_LED_ORANGE);
#endif
    }

    telemetry_packet dummy_packet = { .data.i = 0, .timestamp = 0, \
        .source.subsystem_id = 0x1,                                \
        .source.data_type = TELEMETRY_TYPE_INT,                    \
        .source.source_id = 0x1};   

    int runcount = 0;

    /**
     * Try data packet to server
     */
    while (1) {
        
        /* Alternate between two different dummy payloads */ 
        runcount++;
        if(runcount%2 == 0){
            dummy_packet.data.i = 333;
            dummy_packet.timestamp = 444;
        }
        else {
            dummy_packet.data.i = 111;
            dummy_packet.timestamp = 222;
        }

        /* Get packet buffer for data */
        packet = csp_buffer_get(100);
        if (packet == NULL) {
            /* Could not get buffer element */
            return;
        }

        /* Connect to host HOST, port PORT with regular UDP-like protocol and 1000 ms timeout */
        conn = csp_connect(CSP_PRIO_NORM, TARGET_ADDRESS, MY_PORT, 100, CSP_O_NONE);
        if (conn == NULL) {
            /* Connect failed */
            /* Remember to free packet buffer */
            csp_buffer_free(packet);
            return;
        }

        /* Copy dummy data to packet */
        memcpy(packet->data, &dummy_packet, sizeof(telemetry_packet));

        /* Set packet length */
        packet->length = sizeof(telemetry_packet);

        /* Send packet */
        if (!csp_send(conn, packet, 100)) {
            
            /* Send failed */
            blink(K_LED_RED);
            csp_buffer_free(packet);
        }
        /* success */
        blink(K_LED_GREEN);
        /* Close connection */
        csp_close(conn);
        
        runcount++;
        
        if(runcount == 2147483000)
        runcount = 0;

        csp_sleep_ms(1000);
    }
}


int main(void)
{
    k_uart_console_init();

    #ifdef TARGET_LIKE_STM32
    k_gpio_init(K_LED_GREEN, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_ORANGE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_RED, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_BLUE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_BUTTON_0, K_GPIO_INPUT, K_GPIO_PULL_NONE);
    #endif

    #ifdef TARGET_LIKE_MSP430
    k_gpio_init(K_LED_GREEN, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_RED, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_BUTTON_0, K_GPIO_INPUT, K_GPIO_PULL_UP);
    /* Stop the watchdog. */
    WDTCTL = WDTPW + WDTHOLD;

    __enable_interrupt();

    P2OUT = BIT1;
    #endif

    struct usart_conf conf;

    /* set the device in KISS / UART interface */
    char dev = (char)YOTTA_CFG_CSP_UART_BUS;
    conf.device = &dev;
    usart_init(&conf);

    /* init kiss interface */
    csp_kiss_init(&csp_if_kiss, &csp_kiss_driver, usart_putc, usart_insert, "KISS");

    /* Setup callback from USART RX to KISS RS */
    void my_usart_rx(uint8_t * buf, int len, void * pxTaskWoken) {
        csp_kiss_rx(&csp_if_kiss, buf, len, pxTaskWoken);
    }
    usart_set_callback(my_usart_rx);

    /* csp buffer must be 256, or mtu in csp_iface must match */
    csp_buffer_init(5, 256);
    csp_init(MY_ADDRESS);
    /* set to route through KISS / UART */
    csp_route_set(TARGET_ADDRESS, &csp_if_kiss, CSP_NODE_MAC);
    csp_route_start_task(500, 1);

    xTaskCreate(csp_server, "CSPSRV", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(csp_client, "CSPCLI", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1);

    return 0;
}
