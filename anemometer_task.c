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
int anemometer_initialize_temperature_sensor(void);
int anemometer_validate_gpio_set(void);
long int anemometer_get_default_temperature(void);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

// global variables
ANEMOMETER_INITIALIZATION_T initialization_table[] =
{
    //{initialize_climate_metrics,                false},
    //{initialize_hvac_control,                   false},    
    //{powerwall_init,                            false}, 
    //{anemometer_initialize_buttons,             false}, 
    //{anemometer_display_initialize,             false}, 
    //{anemometer_initialize_temperature_sensor,  false}             
};
bool buttons_initialized = false;

/*!
 * \brief Monitor temperature and control hvac system based on schedule
 *
 * \param params unused garbage
 * 
 * \return nothing
 */
void anemometer_task(void *params)
{
    int ath10_error = 0;
    int tm1637_error = 0;
    int i2c_bytes_written = 0;
    int i2c_bytes_read = 0;
    long int temperaturex10 = 0;
    long int humidityx10 = 0;
    long int moving_avaerage_temperaturex10;
    int retry = 0;
    int oneshot = false;
    int i;
    bool button_pressed = false;

    if (strcasecmp(APP_NAME, "Thermostat") == 0)
    {
        // single purpose application -- force personality and enable
        config.personality = HVAC_THERMOSTAT;
        // config.anemometer_enable = 1;
    }

    printf("anemometer_task started!\n");

    // // set initial status
    // // temperaturex10 = anemometer_get_default_temperature();   
    // // web.powerwall_grid_status = GRID_UNKNOWN;

    // // check and correct critical user configuration settings
    // anemometer_sanitize_user_config();

    // // create the schedule grid used in web inteface
    // make_schedule_grid();
     
    while (true)
    {
    //     if ((config.personality == HVAC_THERMOSTAT))  // TODO should this be config.anemometer_enable ?
    //     {
    //         // check user configured gpios
    //         anemometer_validate_gpio_set();
            
    //         // initialize all subsystems that are not already up
    //         anemometer_initialize();

    //         // measure temperature
    //         ath10_error = aht10_measurement(&temperaturex10, &humidityx10);

    //         if (ath10_error)
    //         {
    //             printf("aht10: i2c error occured will attempt soft reset\n");              
    //             anemometer_deinitialize(anemometer_initialize_temperature_sensor);
    //             temperaturex10 = TEMPERATURE_INVALID;
    //         }
    //         else
    //         {                
    //             // record history
    //             accumlate_metrics(unix_time, temperaturex10, humidityx10);
    //             log_climate_change(temperaturex10, humidityx10);
    //             track_hvac_extrema(COOLING_LAG, temperaturex10);
    //             track_hvac_extrema(HEATING_LAG, temperaturex10);                 

    //             // update web ui
    //             //web.anemometer_temperature = filter_temperature_noise(temperaturex10);
    //             // web.anemometer_temperature = temperaturex10;
    //         }
            
    //         // check powerwall status
    //         // powerwall_check();

    //         // set hvac relays
    //         // control_anemometer_relays(temperaturex10);

    //         if (buttons_initialized)
    //         {
    //             // process button presses until a period of inactivity occurs
    //             button_pressed = handle_button_press_with_timeout(ANEMOMETER_TASK_LOOP_DELAY);
    //         }
    //         else
    //         {
    //             SLEEP_MS(ANEMOMETER_TASK_LOOP_DELAY); 
    //         }

    //         // update web schedule
    //         make_schedule_grid();
    //     }
    //     else
    //     {
    //         SLEEP_MS(60000); 
    //     }   


        printf("anemometeris alive!\n");
        SLEEP_MS(10000);

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
    // int gpio_list[10];
    // bool relay_gpio_valid = false;
    // bool ath10_gpio_valid = false;
    // bool display_gpio_valid = false;
    // bool button_gpio_valid = false;    

    // // relays
    // gpio_list[0] = config.cooling_gpio;
    // gpio_list[1] = config.heating_gpio;
    // gpio_list[2] = config.fan_gpio;

    // // temperature sensor
    // gpio_list[3] = config.anemometer_temperature_sensor_clock_gpio;
    // gpio_list[4] = config.anemometer_temperature_sensor_data_gpio;

    // // display
    // gpio_list[5] = config.anemometer_seven_segment_display_clock_gpio;
    // gpio_list[6] = config.anemometer_seven_segment_display_data_gpio;

    // // front panel buttons
    // gpio_list[7] = config.anemometer_increase_button_gpio;
    // gpio_list[8] = config.anemometer_decrease_button_gpio;
    // gpio_list[9] = config.anemometer_mode_button_gpio;

    // // check for gpio conflicts
    // if (!gpio_conflict(gpio_list, 10))
    // {
    //     // no conflicts
    //     relay_gpio_valid = true;
    //     ath10_gpio_valid = true;
    //     display_gpio_valid = true;
    //     button_gpio_valid = true;
    // }
    // else
    // {
    //     // conflicts found
    //     relay_gpio_valid = false;
    //     ath10_gpio_valid = false;
    //     display_gpio_valid = false;
    //     button_gpio_valid = false;

    //     // incrementally expand list to find non-conflicting functions
    //     if (gpio_conflict(gpio_list, 3))
    //     {
    //         relay_gpio_valid = true;
    //     } 

    //     if (gpio_conflict(gpio_list, 5))
    //     {
    //         ath10_gpio_valid = true;
    //     }   
        
    //     if (gpio_conflict(gpio_list, 7))
    //     {
    //         display_gpio_valid = true;
    //     }  
        
    //     if (gpio_conflict(gpio_list, 10))
    //     {
    //         button_gpio_valid = true;
    //     }         
    // }

    // // check gpios are valid
    // if (!gpio_valid(config.cooling_gpio) || !gpio_valid(config.heating_gpio) || !gpio_valid(config.fan_gpio))
    // {
    //     relay_gpio_valid = false;
    // }

    // if (!gpio_valid(config.anemometer_temperature_sensor_clock_gpio) || !gpio_valid(config.anemometer_temperature_sensor_data_gpio))
    // {
    //     ath10_gpio_valid = false;
    // }

    // if (!gpio_valid(config.anemometer_seven_segment_display_clock_gpio) || !gpio_valid(config.anemometer_seven_segment_display_data_gpio))
    // {
    //     display_gpio_valid = false;
    // }

    // if (!gpio_valid(config.anemometer_increase_button_gpio) || !gpio_valid(config.anemometer_decrease_button_gpio) || !gpio_valid(config.anemometer_mode_button_gpio))
    // {
    //     button_gpio_valid = false;
    // }    

    // // tell subsystems they can use gpio
    // relay_gpio_enable(relay_gpio_valid);
    // ath10_gpio_enable(ath10_gpio_valid);
    // display_gpio_enable(display_gpio_valid);
    // button_gpio_enable(button_gpio_valid);

    return(0);
}


/*!
 * \brief initialize temperature sensor
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_initialize_temperature_sensor(void)
{
    int ath10_error = 0;

    //ath10_error = aht10_initialize(config.anemometer_temperature_sensor_clock_gpio, config.anemometer_temperature_sensor_data_gpio);

    return(ath10_error);
}

/*!
 * \brief initialize front panel buttons
 *
 * \param params none
 * 
 * \return 0 on success
 */
int anemometer_initialize_buttons(void)
{
    int button_error = 0;

    //button_error = initialize_physical_buttons(config.anemometer_mode_button_gpio, config.anemometer_increase_button_gpio, config.anemometer_decrease_button_gpio);    

    if (!button_error)
    {
        buttons_initialized = true;
    }

    return(button_error);
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