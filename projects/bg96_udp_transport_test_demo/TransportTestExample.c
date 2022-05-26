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

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Demo Specific configs. */
#include "demo_config.h"

/* Transport interface implementation include header. */
#include "using_plaintext_udp.h"

/* Include FreeRTOS integration tests header files. */
#include "transport_interface.h"
#include "qualification_test.h"
#include "transport_interface_test.h"
#include "network_connection.h"
#include "platform_function.h"
#include "semphr.h"
#include "event_groups.h"
#include "test_param_config.h"

/*-----------------------------------------------------------*/

/**
 * @brief The task used to demonstrate the transport test.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
void RunTransportTestTask( void * pvParameters );

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer to the type of your desired transport.
 * When using multiple transports in the same compilation unit, define this pointer as void *.
 *
 * @note Transport stacks are defined in Lab-Project-FreeRTOS-Cellular-Demo\source\transport.
 */
struct NetworkContext
{
    PlaintextTransportParams_t * pParams;
};

typedef struct TaskParam
{
    StaticSemaphore_t joinMutexBuffer;
    SemaphoreHandle_t joinMutexHandle;
    FRTestThreadFunction_t threadFunc;
    void * pParam;
    TaskHandle_t taskHandle;
} TaskParam_t;

EventGroupHandle_t xSystemEvents = NULL;
NetworkContext_t xNetworkContext;
PlaintextTransportParams_t xTransportParams = { 0 };
NetworkContext_t xSecondNetworkContext;
PlaintextTransportParams_t xSecondTransportParams = { 0 };
TransportInterface_t xTransport;

static void ThreadWrapper( void * pParam )
{
    TaskParam_t * pTaskParam = pParam;

    if( ( pTaskParam != NULL ) && ( pTaskParam->threadFunc != NULL ) && ( pTaskParam->joinMutexHandle != NULL ) )
    {
        pTaskParam->threadFunc( pTaskParam->pParam );

        /* Give the mutex. */
        xSemaphoreGive( pTaskParam->joinMutexHandle );
    }

    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/

FRTestThreadHandle_t FRTest_ThreadCreate( FRTestThreadFunction_t threadFunc,
                                          void * pParam )
{
    TaskParam_t * pTaskParam = NULL;
    FRTestThreadHandle_t threadHandle = NULL;
    BaseType_t xReturned;

    pTaskParam = malloc( sizeof( TaskParam_t ) );
    configASSERT( pTaskParam != NULL );

    pTaskParam->joinMutexHandle = xSemaphoreCreateBinaryStatic( &pTaskParam->joinMutexBuffer );
    configASSERT( pTaskParam->joinMutexHandle != NULL );

    pTaskParam->threadFunc = threadFunc;
    pTaskParam->pParam = pParam;

    xReturned = xTaskCreate( ThreadWrapper,    /* Task code. */
                             "ThreadWrapper",  /* All tasks have same name. */
                             4096,             /* Task stack size. */
                             pTaskParam,       /* Where the task writes its result. */
                             tskIDLE_PRIORITY, /* Task priority. */
                             &pTaskParam->taskHandle );
    configASSERT( xReturned == pdPASS );

    threadHandle = pTaskParam;

    return threadHandle;
}

/*-----------------------------------------------------------*/

int FRTest_ThreadTimedJoin( FRTestThreadHandle_t threadHandle,
                            uint32_t timeoutMs )
{
    TaskParam_t * pTaskParam = threadHandle;
    BaseType_t xReturned;
    int retValue = 0;

    /* Check the parameters. */
    configASSERT( pTaskParam != NULL );
    configASSERT( pTaskParam->joinMutexHandle != NULL );

    /* Wait for the thread. */
    xReturned = xSemaphoreTake( pTaskParam->joinMutexHandle, pdMS_TO_TICKS( timeoutMs ) );

    if( xReturned != pdTRUE )
    {
        LogError( ( "Waiting thread exist failed after %u %d. Task abort.", timeoutMs, xReturned ) );

        /* Return negative value to indicate error. */
        retValue = -1;

        /* There may be used after free. Assert here to indicate error. */
        configASSERT( FALSE );
    }

    free( pTaskParam );

    return retValue;
}

/*-----------------------------------------------------------*/

void FRTest_TimeDelay( uint32_t delayMs )
{
    vTaskDelay( pdMS_TO_TICKS( delayMs ) );
}

/*-----------------------------------------------------------*/

void * FRTest_MemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

/*-----------------------------------------------------------*/

void FRTest_MemoryFree( void * ptr )
{
    return vPortFree( ptr );
}

/*-----------------------------------------------------------*/



NetworkConnectStatus_t prvTransportNetworkConnect( void * pNetworkContext,
                                                   TestHostInfo_t * pHostInfo,
                                                   void * pNetworkCredentials )
{
    /* Connect the transport network. */
    NetworkConnectStatus_t xNetStatus = NETWORK_CONNECT_FAILURE;
    PlaintextTransportStatus_t xPlaintextStatus = PLAINTEXT_TRANSPORT_SUCCESS;

    /* Attempt to create an UDP connection. */
    xPlaintextStatus = Plaintext_FreeRTOS_UDP_Connect( pNetworkContext,
                                                       pHostInfo->pHostName,
                                                       pHostInfo->port,
                                                       5000,
                                                       5000 );

    if( xPlaintextStatus == PLAINTEXT_TRANSPORT_SUCCESS )
    {
        xNetStatus = NETWORK_CONNECT_SUCCESS;
    }
    else
    {
        LogError( ( "Plaintext_FreeRTOS_UDP_Connect return fail, xPlaintextStatus=%d", xPlaintextStatus ) );
    }

    return xNetStatus;
}

/*-----------------------------------------------------------*/

static void prvTransportNetworkDisconnect( void * pNetworkContext )
{
    /* Disconnect the transport network. */
    Plaintext_FreeRTOS_UDP_Disconnect( pNetworkContext );
}

/*-----------------------------------------------------------*/

static void prvTransportTestDelay( uint32_t delayMs )
{
    /* Delay function to wait for the response from network. */
    const TickType_t xDelay = delayMs / portTICK_PERIOD_MS;

    vTaskDelay( xDelay );
}

/*-----------------------------------------------------------*/

void SetupTransportTestParam( TransportTestParam_t * pTestParam )
{
    /*Transport test initialization */
    xNetworkContext.pParams = &xTransportParams;
    xSecondNetworkContext.pParams = &xSecondTransportParams;

    xTransport.pNetworkContext = &xNetworkContext;
    xTransport.send = Plaintext_FreeRTOS_sendTo;
    xTransport.recv = Plaintext_FreeRTOS_recvFrom;

    /* Setup pTestParam */
    pTestParam->pTransport = &xTransport;
    pTestParam->pNetworkContext = &xNetworkContext;
    pTestParam->pSecondNetworkContext = &xSecondNetworkContext;

    pTestParam->pNetworkConnect = prvTransportNetworkConnect;
    pTestParam->pNetworkDisconnect = prvTransportNetworkDisconnect;
    pTestParam->pNetworkCredentials = NULL;
}

/*-----------------------------------------------------------*/

/*
 * @brief Just call RunQualificationTest to start the test.
 */
void RunTransportTestTask( void * pvParameters )
{
    ( void ) pvParameters;

    RunQualificationTest();
}

/*-----------------------------------------------------------*/
