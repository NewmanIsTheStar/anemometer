
/**
 * Copyright (c) 2024 NewmanIsTheStar
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>


#include "hardware/pio.h"
#include "hardware/clocks.h"
// #include "generated/ws2812.pio.h"

// TODO - prune this list of includes
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "pico/util/datetime.h"
//#include "hardware/rtc.h"
#include "hardware/watchdog.h"

#include "lwip/opt.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/sys.h"
#include <lwip/dns.h>


#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/httpd.h"
#include "dhcpserver.h"
#include "dnsserver.h"

#include "time.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

#include "stdarg.h"

// #include "weather.h"
#include "cgi.h"
#include "ssi.h"
#include "flash.h"
#include "utility.h"
#include "config.h"
#include "watchdog.h"
#include "pluto.h"
// #include "led_strip.h"
#include "udp.h"
#include "message.h"
#include "message_defs.h"
#include "web.h"


//#define DEBUG_UDP_MESSAGES

//#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)


typedef struct ANEMOMETER_REMOTE_STATE_STRUCT
{
    SOCKADDR_IN resolved_address;
    TickType_t resolved_at_tick;
    TickType_t requested_at_tick;
    u_int32_t latest_transaction;
    u_int32_t latest_sequence;     
    int wind_speed;
} ANEMOMETER_REMOTE_STATE_T;


//prototypes
int send_wind_speed_request(SOCKADDR_IN sDest);
void initialize_remote_anemometer(void);
int send_wind_speed_confirm(int iError, SOCKADDR_IN sDest, u_int32_t transaction, u_int32_t sequence);
void poll_remote_anemometer(void);
int receive_wind_speed_request(tsWIND_SPEED_RQST *psMsg, SOCKADDR_IN sDest);
int receive_wind_speed_confirm(tsWIND_SPEED_CNFM *psMsg, SOCKADDR_IN sDest);

// external variables
extern uint32_t unix_time;
extern NON_VOL_VARIABLES_T config;
extern WEB_VARIABLES_T web;

//static variables
static ANEMOMETER_REMOTE_STATE_T remote_anemometer_state;
static SOCKET message_socket = 0;
static char message_buffer[128];
static int message_receive_timeout = 5000000;                             // five seconds
static DOUBLE_BUF_INT remote_speed;
static uint32_t last_confirm_unix_time = 0;

/*!
 * \brief process messages sent to port 6969, format defined in message_defs.h
 *
 * \param[in]  params  alive counter that must be incremented periodically to prevent watchdog reset
 * 
 * \return nothing
 */
void message_task(__unused void *params) 
{
    SOCKADDR_IN sClientAddress;  
    int received_bytes = 0;         
    
    printf("message_task started\n");

    // set initial value for last confirm watchdog trigger
    last_confirm_unix_time = unix_time;

    initialize_remote_anemometer();

    message_socket = upd_establish_socket(6969);

    if (message_socket >= 0)
    {
        for(;;)
        {
             // process messages
            received_bytes = udp_receive(message_socket, message_buffer, sizeof(message_buffer), &sClientAddress, message_receive_timeout);

            if (received_bytes >= sizeof(tsMSG_HDR))
            {
                if (check_received_header((tsMSG_HDR *)&message_buffer, sClientAddress) == 0)
                {
                    // process request
                    switch(htonl(((tsMSG_HDR *)&message_buffer)->message))
                    { 
                    case WIND_SPEED_RQST:
                        receive_wind_speed_request((tsWIND_SPEED_RQST *)&message_buffer, sClientAddress);
                        break;
                    case WIND_SPEED_CNFM:
                         receive_wind_speed_confirm((tsWIND_SPEED_CNFM *)&message_buffer, sClientAddress);
                        break;                                                
                    default:
                        printf("unrecognized Rx message ID (%lu)\n", htonl(((tsMSG_HDR *)&message_buffer)->message));
                        break;
                    }

                }
                else
                {
                    printf("unrecognized header\n");
                }
            }
            else
            {
                if (received_bytes > 0)
                {
                printf("runt packet discarded\n");
#ifdef DEBUG_UDP_MESSAGES
                    {
                        int x;
                        char *address = NULL;

                        address = inet_ntoa(sClientAddress.sin_addr);

                        printf("[%s] RX MSG = ", address);
                        for(x=0; x<received_bytes; x++) printf("%0x ", message_buffer[x]);
                        printf("\n");
                    }
#endif           
                }     
            }

            //control_remote_led_strips();
            poll_remote_anemometer();

            if ((unix_time - last_confirm_unix_time) < (60*60))
            {
                // tell watchdog task that we are still alive
                watchdog_pulse((int *)params);   
            }
            else
            {
                send_syslog_message("anemometer", "No confirm sent for one hour so allowing watchdog reset");
            }
        }
    }
}



/*!
 * \brief validate message header
 *
 * \param[in]  psMsg   pointer message
 * \param[in]  sDest   address of sender
 * 
 * \return 0 if valid header, non-zero if invalid
 */
int check_received_header(tsMSG_HDR *psMsg, SOCKADDR_IN sDest)
{
    int iStatus = 0;
    char *address = NULL;

    address = inet_ntoa(sDest.sin_addr);

    // validate header
    switch(htonl(psMsg->version))
    {
        case 1:
            break;

        default:
            printf("Message header unknown version %lu recieved from %s\n", htonl(psMsg->version), address);
            iStatus = -1;
            break;
    }

    switch (htonl(psMsg->message))
    {
        case LED_STRIP_RQST:
        case LED_STRIP_CNFM:
        case WIND_SPEED_RQST:
        case WIND_SPEED_CNFM:
            // TODO:  here we should detect retries and replay previous responses
            STRNCPY(web.led_last_request_ip, address, sizeof(web.led_last_request_ip));
            break;

        default:
            printf("Message header contains unrecognised MsgId %lu recieved from %s\n", htonl(psMsg->message), address);
            iStatus = -1;
            break;
    }   

    return(iStatus);
}



/*!
 * \brief construct address
 *
 * \param[in]   address_string  hostname or ip 
 * \param[in]   port            udp port
 * \param[out]  address         destination address 
 *
 * \return 0 on success
 */
int construct_address(char *address_string, int port, SOCKADDR_IN *address)
{

    int err = -1;
    struct addrinfo hints;
    struct addrinfo *result;
    int s;
    char port_string[12];
    int type;

    sprintf(port_string, "%d", port);
    type = SOCK_DGRAM;    


    /* Obtain address(es) matching host/port */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;      /* Allow IPv4 only */
    hints.ai_socktype = type;       /* Stream or Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */

    s = getaddrinfo(address_string, port_string, &hints, &result);
    if (s != 0)
    {
        printf("error returned from getaddrinfo [%s, %s, %d]\n", address_string, port_string, type);
    }
    else
    {
        if (result)
        {
            // use first dns result to construct address
            memset(address, 0, sizeof(struct sockaddr_in));
            address->sin_len = sizeof(address);
            address->sin_family = AF_INET;
            address->sin_addr.s_addr = INADDR_ANY;
            address->sin_port = PP_HTONS(port);
            
            // copy address from dns result 
            address->sin_addr = ((struct sockaddr_in *)(result->ai_addr))->sin_addr;

            err = 0;

            // delete linked list of dns results
            freeaddrinfo(result);
        } 
    }   

    return(err);
}


/*!
 * \brief Initialize remote anemometer state variables
 *
 * \param none
 * 
 * \return nothing
 */
void initialize_remote_anemometer(void)
{
    TickType_t tick_now;

    tick_now = xTaskGetTickCount();
    

    memset(&(remote_anemometer_state.resolved_address), 0, sizeof(struct sockaddr_in)); 
    remote_anemometer_state.resolved_at_tick = tick_now;
    remote_anemometer_state.requested_at_tick = tick_now;
    remote_anemometer_state.wind_speed = 0;

    if (config.anemometer_remote_ip[0])
    {
        construct_address(config.anemometer_remote_ip, 6969, &(remote_anemometer_state.resolved_address));
    }
    
}




/*!
 * \brief Poll remote anemometer
 *
 * \param none
 * 
 * \return nothing
 */
void poll_remote_anemometer(void)
{
    int strip;
    int pattern;
    int speed;
    TickType_t tick_now;

    if (config.anemometer_remote_enable)
    {
        speed = get_double_buf_integer(&remote_speed, 0);
        tick_now = xTaskGetTickCount();
            
        if (config.anemometer_remote_ip[0])
        {
            // check time since last resolved the strip address
            if ((tick_now - remote_anemometer_state.resolved_at_tick) > 60000)
            {
                // attempt to resolve ip address
                if (!construct_address(config.anemometer_remote_ip, 6969, &(remote_anemometer_state.resolved_address)))
                {
                    remote_anemometer_state.resolved_at_tick = xTaskGetTickCount();
                }
            }
            // check if pattern and speed already confirmed and one second since last request sent
            if (((tick_now - remote_anemometer_state.requested_at_tick) > 10000))
            {
                //printf("sending led request because: pattern %d vs %d  speed %d vs %d  tick delta = %d\n", remote_led_strip_state[strip].confirmed_pattern, pattern, remote_led_strip_state[strip].confirmed_speed, speed, tick_now - remote_led_strip_state[strip].requested_at_tick);
                // attmpt to send the message
                if (!send_wind_speed_request(remote_anemometer_state.resolved_address))
                {
                    remote_anemometer_state.requested_at_tick = tick_now;
                }
            }
        }

        
    }
}


/*!
 * \brief send request to set led pattern
 *
 * \param[in]  psMsg   pointer message
 * \param[in]  sDest   address of requestor
 * 
 * \return 0 on success
 */
int send_wind_speed_request(SOCKADDR_IN sDest)
{
    tsWIND_SPEED_RQST sRqst;
    int iNumBytes;
    int iError = 0;
    static int sequence = 0;
    int transaction;

    transaction = get_rand_32();

    sRqst.sHeader.version = htonl(1);
    sRqst.sHeader.message = htonl(WIND_SPEED_RQST);
    sRqst.sHeader.transaction = htonl(transaction); 
    sRqst.sHeader.sequence = htonl(sequence);  

    iNumBytes = udp_transmit (message_socket, (char *)&sRqst, sizeof(tsWIND_SPEED_RQST), sDest);    

    if (iNumBytes < 0)
    {
        printf("Failed to send Wind Speed request\n");
        iError = 1;
    }
    else
    {
        remote_anemometer_state.latest_transaction = transaction;
        remote_anemometer_state.latest_sequence = sequence;   

        sequence++;     
    }

    return (iError);
}

/*!
 * \brief set requested led strip pattern and send confirmation message
 *
 * \param[in]  psMsg   pointer message
 * \param[in]  sDest   address of sender
 * 
 * \return 0 on success
 */
int receive_wind_speed_request(tsWIND_SPEED_RQST *psMsg, SOCKADDR_IN sDest)
{
    int iError = 0;

    // compatibility check
    if (htonl(psMsg->sHeader.version) == 1)
    {   
        // send confirmation message
        if (send_wind_speed_confirm(iError, sDest, htonl(psMsg->sHeader.transaction), htonl(psMsg->sHeader.sequence)) > 0)
        {
            // remember the last time we sent a confirm message
            last_confirm_unix_time = unix_time;
        }
    }

    return EXIT_SUCCESS;
}

/*!
 * \brief record confirmed remote led strip pattern in local cache
 *
 * \param[in]  psMsg   pointer message
 * \param[in]  sDest   address of sender
 * 
 * \return 0 on success
 */
int receive_wind_speed_confirm(tsWIND_SPEED_CNFM *psMsg, SOCKADDR_IN sDest)
{
    //int iError = 0;
    int strip;

    // compatibility check
    if (htonl(psMsg->sHeader.version) == 1)
    {
            
        if (remote_anemometer_state.latest_transaction == htonl(psMsg->sHeader.transaction))  //TODO: this relies on transaction being unique, which is not true!
        {
            if (remote_anemometer_state.latest_sequence == htonl(psMsg->sHeader.sequence))
            {
                //printf("Got timely LED confirm from strip %d\n", strip);
            }
            else
            {
                printf("Got late / out of order LED confirm from strip %d\n", strip);
            }

            //TODO: consider checking IP here
            remote_anemometer_state.wind_speed = htonl(psMsg->wind_speed);
            //CLIP(remote_anemometer_state.wind_speed, 0, 320);  //TODO archaic units
            web.anemometer_wind_speed = remote_anemometer_state.wind_speed;
        }              

        
}

    return EXIT_SUCCESS;
}


/*!
 * \brief send confirmed message with current wind speed
 *
 * \param[in]  psMsg   pointer message
 * \param[in]  sDest   address of requestor
 * 
 * \return number of bytes sent
 */
int send_wind_speed_confirm(int iError, SOCKADDR_IN sDest, u_int32_t transaction, u_int32_t sequence)
{
    tsWIND_SPEED_CNFM sCnfm;
    int iNumBytes;

    sCnfm.sHeader.version = htonl(1);
    sCnfm.sHeader.message = htonl(WIND_SPEED_CNFM);
    sCnfm.sHeader.transaction = htonl(transaction);
    sCnfm.sHeader.sequence = htonl(sequence);

    sCnfm.iError = htonl(0);
    sCnfm.wind_speed = htonl(web.anemometer_wind_speed);
    printf("sending wind speed = %d\n",  htonl(sCnfm.wind_speed));

    iNumBytes = udp_transmit (message_socket, (char *)&sCnfm, sizeof(tsWIND_SPEED_CNFM), sDest);

    return(iNumBytes);
}