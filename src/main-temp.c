/**
 *  \file   main.c
 *  \brief  eZ430-RF2500 demo radio, protothread
 *  \author Antoine Fraboulet, Tanguy Risset, Dominique Tournier
 *  \date   2009
 **/

#include <msp430f2274.h>

#if defined(__GNUC__) && defined(__MSP430__)
/* This is the MSPGCC compiler */
#include <msp430.h>
#include <iomacros.h>
#elif defined(__IAR_SYSTEMS_ICC__)
/* This is the IAR compiler */
//#include <io430.h>
#endif

#include <stdio.h>
#include <limits.h>
#include <string.h>

#include "isr_compat.h"
#include "leds.h"
#include "clock.h"
#include "timer.h"
#include "button.h"
#include "uart.h"
#include "adc10.h"
#include "spi.h"
#include "cc2500.h"
#include "flash.h"
#include "watchdog.h"

#include "pt.h"

#define DBG_PRINTF printf


/* 100 Hz timer A */
#define TIMER_PERIOD_MS 10

#define PKTLEN 7
#define MAX_HOPS 3
#define MSG_BYTE_TYPE 0U
#define MSG_BYTE_HOPS 1U
#define MSG_BYTE_SRC_ROUTE 2U
#define MSG_BYTE_CONTENT (MAX_HOPS + 2)
#define MSG_TYPE_ID_REQUEST 0x00
#define MSG_TYPE_ID_REPLY 0x01
#define MSG_TYPE_TEMPERATURE 0x02

#define NODE_ID_LOCATION INFOD_START

#define NODE_ID_UNDEFINED 0x00
/* 10 seconds to reply to an id request */
#define ID_INPUT_TIMEOUT_SECONDS 10
/* the same in timer ticks */
#define ID_INPUT_TIMEOUT_TICKS (ID_INPUT_TIMEOUT_SECONDS*1000/TIMER_PERIOD_MS)
static unsigned char node_id;

#define NUM_TIMERS 6
static uint16_t timer[NUM_TIMERS];
#define TIMER_LED_RED_ON timer[0]
#define TIMER_LED_GREEN_ON timer[1]
#define TIMER_ANTIBOUNCING timer[2]
#define TIMER_RADIO_SEND timer[3]
#define TIMER_ID_INPUT timer[4]
#define TIMER_RADIO_FORWARD timer[5]

static void printhex(char *buffer, unsigned int len)
{
    unsigned int i;
    for(i = 0; i < len; i++)
    {
        printf("%02X ", buffer[i]);
    }
}

static void dump_message(char *buffer)
{
    printf("message received\r\n  content: ");
    printhex(buffer, PKTLEN);
}


/* returns 1 if the id was expected and set, 0 otherwise */
static void set_node_id(unsigned char id)
{
    TIMER_ID_INPUT = UINT_MAX;
    if(flash_write_byte((unsigned char *) NODE_ID_LOCATION, id) != 0)
    {
        flash_erase_segment((unsigned int *) NODE_ID_LOCATION);
        flash_write_byte((unsigned char *) NODE_ID_LOCATION, id);
    }
    node_id = id;
    printf("this node id is now 0x%02X\r\n", id);
}

/* Protothread contexts */

#define NUM_PT 7
static struct pt pt[NUM_PT];


/*
 * Timer
 */

void timer_tick_cb() {
    int i;
    for(i = 0; i < NUM_TIMERS; i++)
    {
        if(timer[i] != UINT_MAX) {
            timer[i]++;
        }
    }
}

int timer_reached(uint16_t timer, uint16_t count) {
    return (timer >= count);
}


/*
 * LEDs
 */

static int led_green_duration;
static int led_green_flag;

/* asynchronous */
static void led_green_blink(int duration)
{
    led_green_duration = duration;
    led_green_flag = 1;
}

static PT_THREAD(thread_led_green(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, led_green_flag);
        led_green_on();
        TIMER_LED_GREEN_ON = 0;
        PT_WAIT_UNTIL(pt, timer_reached(TIMER_LED_GREEN_ON,
          led_green_duration));
        led_green_off();
        led_green_flag = 0;
    }

    PT_END(pt);
}

static PT_THREAD(thread_led_red(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        led_red_switch();
        TIMER_LED_RED_ON = 0;
        PT_WAIT_UNTIL(pt, timer_reached(TIMER_LED_RED_ON, 100));
    }

    PT_END(pt);
}


/*
 * Radio
 */

static char radio_tx_buffer[PKTLEN];
static char radio_rx_buffer[PKTLEN];
static int radio_rx_flag;

void radio_cb(uint8_t *buffer, int size, int8_t rssi)
{
    led_green_blink(10); /* 10 timer ticks = 100 ms */
    DBG_PRINTF("radio_cb :: ");
    switch (size)
    {
        case 0:
            DBG_PRINTF("msg size 0\r\n");
            break;
        case -EEMPTY:
            DBG_PRINTF("msg emptyr\r\n");
            break;
        case -ERXFLOW:
            DBG_PRINTF("msg rx overflow\r\n");
            break;
        case -ERXBADCRC:
            DBG_PRINTF("msg rx bad CRC\r\n");
            break;
        case -ETXFLOW:
            DBG_PRINTF("msg tx overflow\r\n");
            break;
        default:
            if (size > 0)
            {
                /* register next available buffer in pool */
                /* post event to application */
                DBG_PRINTF("rssi %d\r\n", rssi);

                memcpy(radio_rx_buffer, buffer, PKTLEN);
                //FIXME what if radio_rx_flag == 1 already?
                radio_rx_flag = 1;
            }
            else
            {
                /* packet error, drop */
                DBG_PRINTF("msg packet error size=%d\r\n",size);
            }
            break;
    }

    cc2500_rx_enter();
}


static void radio_send_message()
{
    cc2500_utx(radio_tx_buffer, PKTLEN);
    printf("sent: ");
    printhex(radio_tx_buffer, PKTLEN);
    putchar('\r');
    putchar('\n');
    cc2500_rx_enter();
}

static PT_THREAD(thread_receive(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, radio_rx_flag == 1);
	printf("sent: ");
	printhex(radio_tx_buffer, PKTLEN);
	putchar('\r');
	putchar('\n');

        dump_message(radio_rx_buffer);
	radio_rx_flag = 0;

    }

    PT_END(pt);
}


/*
 * UART
 */

static int uart_flag;
static uint8_t uart_data;

int uart_cb(uint8_t data)
{
    uart_flag = 1;
    uart_data = data;
    return 0;
}

/* to be called from within a protothread */
static void init_message()
{
    unsigned int i;
    for(i = 0; i < PKTLEN; i++)
    {
        radio_tx_buffer[i] = 0x00;
    }
    radio_tx_buffer[MSG_BYTE_HOPS] = 0x01;
    radio_tx_buffer[MSG_BYTE_SRC_ROUTE] = node_id;
}

/* to be called from within a protothread */
#ifdef TAG
static void send_temperature()
{
    init_message();
    radio_tx_buffer[MSG_BYTE_TYPE] = MSG_TYPE_TEMPERATURE;
    int temperature = adc10_sample_temp();
    printf("temperature: %d, hex: ", temperature);
    printhex((char *) &temperature, 2);
    putchar('\r');
    putchar('\n');
    /* msp430 is little endian, convert temperature to network order */
    char *pt = (char *) &temperature;
    radio_tx_buffer[MSG_BYTE_CONTENT] = pt[1];
    radio_tx_buffer[MSG_BYTE_CONTENT + 1] = pt[0];
    radio_send_message();
}
#endif

static void send_id_request()
{
    init_message();
    radio_tx_buffer[MSG_BYTE_TYPE] = MSG_TYPE_ID_REQUEST;
    radio_send_message();
}


static void send_id_reply(unsigned char id)
{
    init_message();
    radio_tx_buffer[MSG_BYTE_TYPE] = MSG_TYPE_ID_REPLY;
    radio_tx_buffer[MSG_BYTE_CONTENT] = id;
    radio_send_message();
    printf("ID 0x%02X sent\r\n", id);
}

static PT_THREAD(thread_uart(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, uart_flag);

        led_green_blink(10); /* 10 timer ticks = 100 ms */

        /* does the local node expects an id
         * or do we have to broadcast it? */
        if(timer_reached(TIMER_ID_INPUT, ID_INPUT_TIMEOUT_TICKS))
        {
            send_id_reply(uart_data);
        }
        else
        {
            set_node_id(uart_data);
        }
        uart_flag = 0;
    }

    PT_END(pt);
}

#define ANTIBOUNCING_DURATION 10 /* 10 timer counts = 100 ms */
static int antibouncing_flag;
static int button_pressed_flag;

void button_pressed_cb()
{
    if(antibouncing_flag == 0)
    {
        button_pressed_flag = 1;
        antibouncing_flag = 1;
        TIMER_ANTIBOUNCING = 0;
        led_green_blink(200); /* 200 timer ticks = 2 seconds */
    }
}

static PT_THREAD(thread_button(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, button_pressed_flag == 1);

        TIMER_ID_INPUT = 0;

        /* ask locally for a node id and broadcast an id request */
        prompt_node_id();
        send_id_request();

        button_pressed_flag = 0;
    }


    PT_END(pt);
}

static PT_THREAD(thread_antibouncing(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, antibouncing_flag
          && timer_reached(TIMER_ANTIBOUNCING, ANTIBOUNCING_DURATION));
        antibouncing_flag = 0;
    }

    PT_END(pt);
}


#ifdef TAG
static PT_THREAD(thread_periodic_send(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        TIMER_RADIO_SEND = 0;
        PT_WAIT_UNTIL(pt, node_id != NODE_ID_UNDEFINED && timer_reached( TIMER_RADIO_SEND, 1000));
        send_temperature();
    }

    PT_END(pt);
}
#endif

/*
 * main
 */

int main(void)
{
    watchdog_stop();

    TIMER_ID_INPUT = UINT_MAX;
    node_id = NODE_ID_UNDEFINED;

    /* protothreads init */
    int i;
    for(i = 0; i < NUM_PT; i++)
    {
        PT_INIT(&pt[i]);
    }

    /* clock init */
    set_mcu_speed_dco_mclk_16MHz_smclk_8MHz();

    /* LEDs init */
    leds_init();
    led_red_on();
    led_green_flag = 0;

    /* timer init */
    timerA_init();
    timerA_register_cb(&timer_tick_cb);
    timerA_start_milliseconds(TIMER_PERIOD_MS);


    /* button init */
    button_init();
    button_register_cb(button_pressed_cb);
    antibouncing_flag = 0;
    button_pressed_flag = 0;

    /* UART init (serial link) */
    uart_init(UART_9600_SMCLK_8MHZ);
    uart_register_cb(uart_cb);
    uart_flag = 0;
    uart_data = 0;

    /* ADC10 init (temperature) */
    adc10_start();

    /* radio init */
    spi_init();
    cc2500_init();
    cc2500_rx_register_buffer(radio_tx_buffer, PKTLEN);
    cc2500_rx_register_cb(radio_cb);
    cc2500_rx_enter();
    radio_rx_flag = 0;

    /* retrieve node id from flash */
    node_id = *((char *) NODE_ID_LOCATION);
    printf("node id retrieved from flash: %d\r\n", node_id);

    __enable_interrupt();

    /* simple cycle scheduling */
    while(1) {
        thread_led_red(&pt[0]);
        thread_led_green(&pt[1]);
        thread_uart(&pt[2]);
        thread_antibouncing(&pt[3]);
#ifdef ANCHOR
        thread_receive(&pt[4]);
#endif
#ifdef TAG
        thread_periodic_send(&pt[5]);
#endif
        thread_button(&pt[6]);
    }
}
