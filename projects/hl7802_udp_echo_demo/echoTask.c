/*
 * FreeRTOS V202011.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * Demo for showing use of the MQTT API using a mutually authenticated
 * network connection.
 *
 * The Example shown below uses MQTT APIs to create MQTT messages and send them
 * over the mutually authenticated network connection established with the
 * MQTT broker. This example is single threaded and uses statically allocated
 * memory. It uses QoS1 for sending to and receiving messages from the broker.
 *
 * A mutually authenticated TLS connection is used to connect to the
 * MQTT message broker in this example. Define democonfigMQTT_BROKER_ENDPOINT,
 * democonfigROOT_CA_PEM, democonfigCLIENT_CERTIFICATE_PEM,
 * and democonfigCLIENT_PRIVATE_KEY_PEM in demo_config.h to establish a
 * mutually authenticated connection.
 */

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Demo Specific configs. */
#include "demo_config.h"

/* Include BG96 configuration to get CELLULAR_MAX_SEND_DATA_LEN. */
#include "cellular_config.h"
#include "cellular_config_defaults.h"

/* Transport interface implementation include header. */
#include "using_plaintext_udp.h"

/*-----------------------------------------------------------*/

/**
 * @brief Endpoint of the echo server to connect to in transport interface test.
 *
 * #define ECHO_SERVER_ENDPOINT   "PLACE_HOLDER"
 */
#ifndef ECHO_SERVER_ENDPOINT
    #define ECHO_SERVER_ENDPOINT   "PLACE_HOLDER"
#endif

/**
 * @brief Port of the echo server to connect to in transport interface test.
 *
 * #define ECHO_SERVER_PORT       (9000)
 */
#ifndef ECHO_SERVER_PORT
    #define ECHO_SERVER_PORT   (9000)
#endif

/**
 * @brief Timeout for sending/receiving packets in milliseconds.
 *
 * #define ECHO_SEND_RECV_TIMEOUT_MS       (5000)
 */
#ifndef ECHO_SEND_RECV_TIMEOUT_MS
    #define ECHO_SEND_RECV_TIMEOUT_MS       (5000)
#endif

/**
 * @brief The maximum length of socket buffer.
 *
 * #define ECHO_BUFFER_MAX_SIZE       (CELLULAR_MAX_SEND_DATA_LEN)
 */
#ifndef ECHO_BUFFER_SIZE
    #define ECHO_BUFFER_MAX_SIZE       (CELLULAR_MAX_SEND_DATA_LEN)
#endif

/**
 * @brief The maximum retry time to re-send the packet.
 *
 * #define ECHO_MAX_RETRY_COUNT       (3)
 */
#ifndef ECHO_MAX_RETRY_COUNT
    #define ECHO_MAX_RETRY_COUNT       (10)
#endif

/*-----------------------------------------------------------*/

typedef struct {
    uint8_t sendBuf[ECHO_BUFFER_MAX_SIZE];
    uint8_t recvBuf[ECHO_BUFFER_MAX_SIZE];
} echoTestBuffer_t;

echoTestBuffer_t xEchoTestBuffer;

/**
 * @brief The task used to demonstrate the echo with UDP.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
void RunEchoTask( void * pvParameters );

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer to the type of your desired transport.
 * When using multiple transports in the same compilation unit, define this pointer as void *.
 *
 * @note Transport stacks are defined in Lab-Project-FreeRTOS-Cellular-Demo\source\transport.
 */
struct NetworkContext
{
    PlaintextTransportParams_t* pParams;
};

bool prvTransportNetworkConnect(void* pNetworkContext )
{
    /* Connect the transport network. */
    bool xNetStatus = false;
    PlaintextTransportStatus_t xPlaintextStatus = PLAINTEXT_TRANSPORT_SUCCESS;

    /* Attempt to create an UDP connection. */
    xPlaintextStatus = Plaintext_FreeRTOS_UDP_Connect( pNetworkContext,
                                                       ECHO_SERVER_ENDPOINT,
                                                       ECHO_SERVER_PORT,
                                                       ECHO_SEND_RECV_TIMEOUT_MS,
                                                       ECHO_SEND_RECV_TIMEOUT_MS);

    if (xPlaintextStatus == PLAINTEXT_TRANSPORT_SUCCESS)
    {
        xNetStatus = true;
    }
    else 
    {
        LogError( ( "Plaintext_FreeRTOS_UDP_Connect return fail, xPlaintextStatus=%d", xPlaintextStatus ) );
    }

    return xNetStatus;
}

/*-----------------------------------------------------------*/

static void prvTransportNetworkDisconnect(void* pNetworkContext)
{
    /* Disconnect the transport network. */
    Plaintext_FreeRTOS_UDP_Disconnect( pNetworkContext );
}

/*-----------------------------------------------------------*/

/**
 * @brief Initialize the test data with 0,1,...,255,0,1,...
 */
static void prvInitializeTestData( uint8_t* pTransportTestBuffer,
                                   size_t testSize )
{
    uint32_t i = 0U;

    for (i = 0U; i < testSize; i++)
    {
        pTransportTestBuffer[i] = (uint8_t)i;
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Send the packet to the echo server.
 */
static bool prvSendPackets(void* pNetworkContext,
    uint8_t* pBuf,
    size_t size)
{
    bool result = false;
    int32_t sentByte = 0;

    sentByte = Plaintext_FreeRTOS_sendTo(pNetworkContext, pBuf, size);

    if (sentByte == size)
    {
        result = true;
    }

    return result;
}

/*-----------------------------------------------------------*/

/**
 * @brief Receive the packet from the echo server.
 */
static bool prvRecvPackets(void* pNetworkContext,
    uint8_t* pBuf,
    int32_t size)
{
    bool result = false;
    int32_t recvByte = 0;

    do
    {
        recvByte = Plaintext_FreeRTOS_recvFrom(pNetworkContext, pBuf, size);

        if (recvByte == size)
        {
            result = true;
            break;
        }
    } while( recvByte > 0 );
    

    return result;
}

/*-----------------------------------------------------------*/

static bool prvLoopSendAndReceive(void* pNetworkContext)
{
    bool result = true;
    bool recvResult = true;
    bool sendResult = true;
    int32_t size = 1;
    uint16_t failCount = 0;

    /* Loop from 1~ECHO_BUFFER_MAX_SIZE. */
    for (size = 10; size <= ECHO_BUFFER_MAX_SIZE; )
    {
        /* Send the packet */
        sendResult = prvSendPackets( pNetworkContext, xEchoTestBuffer.sendBuf, size );

        if (!sendResult)
        {
            LogError( ( "Send UDP packet failed" ) );
            result = false;
            break;
        }

        /* Recv & compare the packet. */
        recvResult = prvRecvPackets(pNetworkContext, xEchoTestBuffer.recvBuf, size );

        /* It's possible to miss packet via UDP protocol, try to send again. */
        if( !recvResult )
        {
            failCount++;
            LogWarn(("Recv UDP packet failed, count=%d", failCount));

            if (failCount > ECHO_MAX_RETRY_COUNT)
            {
                LogError(("Reach max retry count, recv UDP packet failed"));
                result = false;
                break;
            }
        }
        else
        {
            if (memcmp(xEchoTestBuffer.sendBuf, xEchoTestBuffer.recvBuf, size) == 0)
            {
                failCount = 0;
                size++;
            }
            else
            {
                LogError( ( "Compare send/recv buffer failed" ) );
                result = false;
                break;
            }
        }

        memset(xEchoTestBuffer.recvBuf, 0, sizeof(xEchoTestBuffer.recvBuf));
    }

    return result;
}

/*-----------------------------------------------------------*/

/*
 * @brief The entry to run echo with UDP socket.
 */
void RunEchoTask( void * pvParameters )
{
    NetworkContext_t xNetworkContext = { 0 };
    PlaintextTransportParams_t xPlaintextTransportParam = { 0 };
    bool status = false;

    (void) pvParameters;

    /* Initialize the network context. */
    xNetworkContext.pParams = &xPlaintextTransportParam;
    prvInitializeTestData( xEchoTestBuffer.sendBuf, sizeof(xEchoTestBuffer.sendBuf) );
    prvInitializeTestData( xEchoTestBuffer.recvBuf, sizeof(xEchoTestBuffer.recvBuf) );

    /* Create the socket connection */
    status = prvTransportNetworkConnect( &xNetworkContext );

    if (status)
    {
        /* Loop to send & receive data with echo server. */
        status = prvLoopSendAndReceive(&xNetworkContext);
    }

    if (!status)
    {
        LogError( ( "============ Demo Failed ============" ) );
    }
    else
    {
        LogInfo( ( "============ Demo Pass ============" ) );
    }
    
    /* Release the resources. */
    prvTransportNetworkDisconnect( &xNetworkContext );

    /* Test finished. */
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------*/