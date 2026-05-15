/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
//#include "hardware/rtc.h"
#include "hardware/watchdog.h"
#include <hardware/flash.h>
#include "hardware/i2c.h"
#include "hardware/adc.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include "stdarg.h"

#include "watchdog.h"
#include "weather.h"
#include "anemometer.h"
#include "flash.h"
#include "calendar.h"
#include "utility.h"
#include "config.h"
#include "led_strip.h"
#include "message.h"
// #include "altcp_tls_mbedtls_structs.h"
// #include "powerwall.h"
#include "pluto.h"
// #include "tm1637.h"

#define WIND_SPEED_MOVING_AVERAGE_NUM_SAMPLES (3)

// typdedefs
typedef struct
{
    int (*initialization)(void);
    bool initialization_complete;
} ANEMOMETER_INITIALIZATION_T;

// prototypes
int anemometer_sanitize_user_config(void);
int anemometer_initialize(void);
int anemometer_deinitialize(int (*subsytem_init_func)(void));
int anemometer_initialize_buttons(void);
int anemometer_initialize_adc(void);
int anemometer_validate_gpio_set(void);
long int anemometer_get_default_temperature(void);
int anemometer_get_moving_average_wind_speed(int instantaneous_wind_speed);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
ANEMOMETER_INITIALIZATION_T initialization_table[] =
{
    {anemometer_initialize_adc,                false},
};
bool adc_initialized = false;
bool adc_gpio_valid = false;
int adc_input = 0;

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
void anemometer_task(void *params)
{
    uint16_t result;
    int lowest_adc_reading = 819;
    int highest_adc_reading = 4095;
    int range_of_adc_readings = 4095 - 819;
    static int last_logged_wind_speed = 0;
    static uint32_t last_logged_wind_speed_time = 0;

    if (strcasecmp(APP_NAME, "Anemometer") == 0)
    {
        // single purpose application -- force personality and enable
        config.personality = ANEMOMETER;
        config.anemometer_enable = 1;
    }

    printf("anemometer_task started!\n");

    sprintf(web.stack_message, "Measuring wind speed");

    while (true)
    {
        // check user configured gpios
        anemometer_validate_gpio_set();
        
        // initialize all subsystems that are not already up
        anemometer_initialize();      

        if (adc_initialized)
        {
            // read the raw ADC value
            result = adc_read();
            
            // Print the value to the console
            printf("Raw ADC value: %u\t", result);

            if (result > highest_adc_reading)
            {
                highest_adc_reading = result;
            }

            if (result < lowest_adc_reading)
            {
                lowest_adc_reading = result;
            }

            // update web interface
            web.anemometer_adc_max = highest_adc_reading;
            web.anemometer_adc_min = lowest_adc_reading;

            range_of_adc_readings = highest_adc_reading - lowest_adc_reading;

            if (result < 819)
            {
                // at or below minimum measurable wind speed
                web.anemometer_wind_speed = 0;
            }
            else
            {
                // wind speed = (I-4)/16*A+B  where I = current in mA, A = wind speed range (0 to 45m/s), B = lowest wind speed (0.8 m/s)
                web.anemometer_wind_speed = ((((result - lowest_adc_reading)*100)/range_of_adc_readings)*45 + 80)/10;            
            }

            // compute moving average
            web.anemometer_wind_speed = anemometer_get_moving_average_wind_speed(web.anemometer_wind_speed);

            if ((web.anemometer_wind_speed != last_logged_wind_speed) && ((unix_time - last_logged_wind_speed_time) > (60*10-1)))
            {                
                send_syslog_message("anemometer", "Wind Speed = %c%ld.%ld m/s\n", web.anemometer_wind_speed<0?'-':' ', abs(web.anemometer_wind_speed)/10, abs(web.anemometer_wind_speed%10));
                last_logged_wind_speed = web.anemometer_wind_speed;
                last_logged_wind_speed_time = unix_time;
            }

            printf("Wind Speed = %c%ld.%ld m/s\n", web.anemometer_wind_speed<0?'-':' ', abs(web.anemometer_wind_speed)/10, abs(web.anemometer_wind_speed%10));
        }
 
        SLEEP_MS(1000);

        // tell watchdog task that we are still alive
        watchdog_pulse((int *)params);               
    }
}

/*!
 * \brief Validate set of GPIOs
 *
 * \param params max_set
 * 
 * \return nothing
 */
int anemometer_validate_gpio_set(void)
{
    switch(config.anemometer_adc_gpio)
    {
    case 26:
        adc_input = 0;
        adc_gpio_valid = true;
        break;
    case 27:
        adc_input = 1;
        adc_gpio_valid = true;
        break;
    case 28:
        adc_input = 2;
        adc_gpio_valid = true;
        break;
    case 29:
        adc_input = 3;
        adc_gpio_valid = true;
        break;
    default:
        adc_input = -1;
        adc_gpio_valid = false;
        break;                        
    }

    return(0);
}


/*!
 * \brief initialize temperature sensor
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_initialize_adc(void)
{
    int adc_error = 1;

    if (adc_gpio_valid)
    {
        // initialize the ADC
        adc_init();

        // enable the ADC pin (GPIO26 is channel 0)
        adc_gpio_init(config.anemometer_adc_gpio);

        // Select ADC input channel 0 (GPIO26)
        adc_select_input(adc_input); 

        adc_initialized = true;
        adc_error = 0;
    }

    return(adc_error);
}


/*!
 * \brief initialize subsystems
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_initialize(void)
{
    int err = 0;
    int i;

    for (i=0; i < NUM_ROWS(initialization_table); i++)
    {
        if (!initialization_table[i].initialization_complete)
        {
            initialization_table[i].initialization_complete = !initialization_table[i].initialization();

            if (!initialization_table[i].initialization_complete)
            {
                err++;
                printf("Error initializing subsystem %d\n", i);
            }
        }
    }

    if (err)
    {
        printf("%d subsystems failed to initialize\n", err);
    }

    return(err);
}

/*!
 * \brief deinitialize a subsytem
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_deinitialize(int (*subsytem_init_func)(void))
{
    int err = 1;
    int i;

    for (i=0; i < NUM_ROWS(initialization_table); i++)
    {
        if (initialization_table[i].initialization == subsytem_init_func)
        {
            initialization_table[i].initialization_complete = false;
            err = 0;
            break;
        }
    }

    return(err);
}

/*!
 * \brief deinitialize a subsytem
 *
 * \param params none
 * 
 * \return 0 on success
 */
long int anemometer_get_default_temperature(void)
{
    long int temperaturex10 = 0;

    if (config.use_archaic_units)
    {
        temperaturex10 = SETPOINT_DEFAULT_FAHRENHEIT_X_10;
    }
    else
    {
        temperaturex10 = SETPOINT_DEFAULT_CELSIUS_X_10;
    }    

    return(temperaturex10);
}


 /*!
 * \brief perform sanity check on critical user config values
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_sanitize_user_config(void)
{   
    // // make sure safeguards are valid to prevent short cycling
    // CLIP(config.heating_to_cooling_lockout_mins, 1, 60);
    // CLIP(config.minimum_heating_on_mins, 1, 60);
    // CLIP(config.minimum_cooling_on_mins, 1, 60);
    // CLIP(config.minimum_heating_off_mins, 1, 60);
    // CLIP(config.minimum_cooling_off_mins, 1, 60);
    // if (config.use_archaic_units)
    // {
    //     CLIP(config.anemometer_hysteresis, 10, 100);  // 1 F to 10 F
    // }
    // else
    // {
    //     CLIP(config.anemometer_hysteresis, 5, 50);   // 0.5 C to 5 C       
    // }

    return(0);
}

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
int anemometer_get_moving_average_wind_speed(int instantaneous_wind_speed)
{
    static int wind_speed_sample[WIND_SPEED_MOVING_AVERAGE_NUM_SAMPLES];
    static int wind_speed_sample_index = 0;
    static int wind_speed_sample_population = 0;
    int moving_average_wind_speed = 0;
    int i;

    wind_speed_sample[wind_speed_sample_index] = instantaneous_wind_speed;
    wind_speed_sample_index = (wind_speed_sample_index+1)%WIND_SPEED_MOVING_AVERAGE_NUM_SAMPLES;

    if (wind_speed_sample_population < WIND_SPEED_MOVING_AVERAGE_NUM_SAMPLES)
    {
        wind_speed_sample_population++;
    }

    for (i = 0; i < wind_speed_sample_population; i++)
    {
       moving_average_wind_speed += wind_speed_sample[i];
    }

    // compute moving average
    moving_average_wind_speed = moving_average_wind_speed/wind_speed_sample_population;

    return(moving_average_wind_speed);
}