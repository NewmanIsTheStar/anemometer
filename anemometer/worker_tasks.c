/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include "stdarg.h"

#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

#include "worker_tasks.h"

// include header for each worker task here
#include "weather.h"
#include "led_strip.h"
#include "message.h"
#include "anemometer.h"
#include "hc_task.h"
#include "discovery_task.h"

// worker tasks to launch and monitor
WORKER_TASK_T worker_tasks[] =
{
    {   message_task,   "Message Task",         1024,   1},  
    //  function        name                    stack   priority        
#ifdef INCORPORATE_ANEMOMETER    
    {   anemometer_task,"Anemometer Task",      8096,   5},       
#endif

    // end of table
    {   NULL,           NULL,               0,      0,         }
};




