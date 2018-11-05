/**
 *  \file   main.c
 *  \brief  eZ430-RF2500 radio communication demo
 *  \author Antoine Fraboulet, Tanguy Risset, Dominique Tournier, Alexis Saget
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
#define MSG_BYTE_TYPE 0U
#define MSG_BYTE_NODE_ID 1U
#define MSG_BYTE_CONTENT 2U //2U and 3U
#define MSG_BYTE_RANK 4U
#define MSG_BYTE_TARGET 5U
#define MSG_TYPE_ID_REPLY 0x01
#define MSG_TYPE_TEMPERATURE 0x02
#define MSG_TYPE_LED_GREEN 0x03

#define MSG_CONTENT_LED_ON 0x00
#define MSG_CONTENT_LED_OFF 0x01
#define SINK_TARGET 0x00
#define BROADCAST_TARGET 0xFF


#define NODE_ID_LOCATION INFOD_START

#define NODE_ID_UNDEFINED 0x00
/* 10 seconds to reply to an id request */
#define ID_INPUT_TIMEOUT_SECONDS 10
/* the same in timer ticks */
#define ID_INPUT_TIMEOUT_TICKS (ID_INPUT_TIMEOUT_SECONDS*1000/TIMER_PERIOD_MS)
static unsigned char node_id;
static unsigned int node_rank;

#define NUM_TIMERS 6
static uint16_t timer[NUM_TIMERS];
#define TIMER_LED_RED_ON timer[0]
#define TIMER_LED_GREEN_ON timer[1]
#define TIMER_ANTIBOUNCING timer[2]
#define TIMER_RADIO_SEND timer[3]
#define TIMER_ID_INPUT timer[4]
#define TIMER_RADIO_FORWARD timer[5]

const RF_SETTINGS configrf = {
    0x12,   // FSCTRL1   Frequency synthesizer control.
    0x00,   // FSCTRL0   Frequency synthesizer control.
    0x5D,   // FREQ2     Frequency control word, high byte.
    0x93,   // FREQ1     Frequency control word, middle byte.
    0xB1,   // FREQ0     Frequency control word, low byte.
    0x2D,   // MDMCFG4   Modem configuration.
    0x3B,   // MDMCFG3   Modem configuration.
    0xF3,   // MDMCFG2   Modem configuration.
    0x22,   // MDMCFG1   Modem configuration.
    0xF8,   // MDMCFG0   Modem configuration.
    0x04,   // CHANNR    Channel number.
    0x01,   // DEVIATN   Modem deviation setting (when FSK modulation is enabled).
    0xB6,   // FREND1    Front end RX configuration.
    0x10,   // FREND0    Front end TX configuration.
    0x18,   // MCSM0     Main Radio Control State Machine configuration.
    0x1D,   // FOCCFG    Frequency Offset Compensation Configuration.
    0x1C,   // BSCFG     Bit synchronization Configuration.
    0xC7,   // AGCCTRL2  AGC control.
    0x00,   // AGCCTRL1  AGC control.
    0xB0,   // AGCCTRL0  AGC control.
    0xEA,   // FSCAL3    Frequency synthesizer calibration.
    0x0A,   // FSCAL2    Frequency synthesizer calibration.
    0x00,   // FSCAL1    Frequency synthesizer calibration.
    0x11,   // FSCAL0    Frequency synthesizer calibration.
    0x59,   // FSTEST    Frequency synthesizer calibration.
    0x88,   // TEST2     Various test settings.
    0x31,   // TEST1     Various test settings.
    0x0B,   // TEST0     Various test settings.
    0x07,   // FIFOTHR   RXFIFO and TXFIFO thresholds.
    0x29,   // IOCFG2    GDO2 output pin configuration.
    0x06,   // IOCFG0D   GDO0 output pin configuration. Refer to SmartRF® Studio User Manual for detailed pseudo register explanation.
    0x04,   // PKTCTRL1  Packet automation control.
    0x05,   // PKTCTRL0  Packet automation control.
    0x00,   // ADDR      Device address.
    0xFF    // PKTLEN    Packet length.
};

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

    printf("%01X:%01X:%01X:", buffer[MSG_BYTE_TYPE], buffer[MSG_BYTE_RANK], buffer[MSG_BYTE_NODE_ID]);


    if(buffer[MSG_BYTE_TYPE] == MSG_TYPE_TEMPERATURE)
    {
		unsigned int temperature;
    	char *pt = (char *) &temperature;
        pt[0] = buffer[MSG_BYTE_CONTENT + 1];
        pt[1] = buffer[MSG_BYTE_CONTENT];
        printf("%d\r\n", temperature);
        
    }
		if(buffer[MSG_BYTE_TYPE] == MSG_TYPE_LED_GREEN && (buffer[MSG_BYTE_TARGET] == node_id || buffer[MSG_BYTE_TARGET] == BROADCAST_TARGET))
		{
				if(buffer[MSG_BYTE_CONTENT] == MSG_CONTENT_LED_ON)
				{
					printf("ON\r\n");
					//turn green led on
					led_green_on();
				}
				else if(buffer[MSG_BYTE_CONTENT] == MSG_CONTENT_LED_OFF)
				{
					//turn green led off
					printf("OFF\r\n");
					led_green_off();
				}
		}
}

static void prompt_node_id()
{
    printf("A node requested an id. You have %d seconds to enter an 8-bit ID.\r\n", ID_INPUT_TIMEOUT_SECONDS);
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
	printf("%01X:%01X:%01X:%02x", radio_tx_buffer[MSG_BYTE_TYPE], radio_tx_buffer[MSG_BYTE_RANK], radio_tx_buffer[MSG_BYTE_NODE_ID], radio_tx_buffer[MSG_BYTE_CONTENT]);
    putchar('\r');
    putchar('\n');
    cc2500_rx_enter();
}

static PT_THREAD(thread_process_msg(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt, radio_rx_flag == 1);

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
static void init_message(int target)
{
    unsigned int i;
    for(i = 0; i < PKTLEN; i++)
    {
        radio_tx_buffer[i] = 0x00;
    }
    radio_tx_buffer[MSG_BYTE_NODE_ID] = node_id;
    radio_tx_buffer[MSG_BYTE_RANK] = node_rank;
    radio_tx_buffer[MSG_BYTE_TARGET] = target;

}

/* to be called from within a protothread */
static void send_temperature()
{
    init_message(SINK_TARGET);
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

static void send_green_led_state(int state, int target)
{
		init_message(target);
		radio_tx_buffer[MSG_BYTE_TYPE] = MSG_TYPE_LED_GREEN;
		if(state) radio_tx_buffer[MSG_BYTE_CONTENT]=MSG_CONTENT_LED_ON;
		else radio_tx_buffer[MSG_BYTE_CONTENT]=MSG_CONTENT_LED_OFF;
		printf("send green led message: %02x, hex: ", radio_tx_buffer[MSG_BYTE_CONTENT]);
		radio_send_message();
		
}



static void send_id_reply(unsigned char id)
{
    init_message(SINK_TARGET);
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

	set_node_id(uart_data);
        uart_flag = 0;
    }

    PT_END(pt);
}

/*
 * Button
 */

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
        //prompt_node_id();
		if(led_green_flag == 0)
		{
			led_green_flag=1;        
			send_green_led_state(1, BROADCAST_TARGET); 
		}
		else
		{
			led_green_flag=0;
			send_green_led_state(0, BROADCAST_TARGET); 
		}

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
    cc2500_configure(& configrf);
    cc2500_rx_register_buffer(radio_tx_buffer, PKTLEN);
    cc2500_rx_register_cb(radio_cb);
    cc2500_rx_enter();
    radio_rx_flag = 0;

    /* retrieve node id from flash */
    node_id = *((char *) NODE_ID_LOCATION);
#ifdef ANCHOR
    printf("ANCHOR RUNNING: \r\n");
	node_rank = 0;
#endif
#ifdef TAG
    printf("TAG RUNNING: \r\n");
	node_rank = 0xFF;
#endif
    printf("node id retrieved from flash: %d\r\n", node_id);
    button_enable_interrupt();
    __enable_interrupt();

    /* simple cycle scheduling */
    while(1) {
        thread_led_red(&pt[0]);
        //thread_led_green(&pt[1]);
        thread_uart(&pt[2]);
        thread_antibouncing(&pt[3]);
        thread_process_msg(&pt[4]);
#ifdef TAG
        thread_periodic_send(&pt[5]);
#endif
#ifdef ANCHOR
        thread_button(&pt[6]);
#endif
    }
}
