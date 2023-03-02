/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PSoC 6 MCU and OPTIGA Trust M
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
* The MIT License
*
* Copyright (c) 2021 Infineon Technologies AG
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE
*******************************************************************************/

#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

#include "optiga/optiga_util.h"
#include "optiga/optiga_crypt.h"
#include "optiga/pal/pal_os_timer.h"
#include "optiga/pal/pal_logger.h"
#include "optiga/pal/pal.h"
#include "optiga_example.h"

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void optiga_lib_callback(void * context, optiga_lib_status_t return_status);
#ifdef OPTIGA_TRUSTM_VDD
static void example_optiga_util_hibernate_restore(void);
#endif

/*******************************************************************************
* Global Variables
*******************************************************************************/
/**
 * Callback when optiga_util_xxxx operation is completed asynchronously
 */
static volatile optiga_lib_status_t optiga_lib_status;

//SHA-256 Digest to be signed
static const uint8_t digest [] =
{
    0x61, 0xC7, 0xDE, 0xF9, 0x0F, 0xD5, 0xCD, 0x7A,0x8B, 0x7A, 0x36, 0x41, 0x04, 0xE0, 0x0D, 0x82,
    0x38, 0x46, 0xBF, 0xB7, 0x70, 0xEE, 0xBF, 0x8F,0x40, 0x25, 0x2E, 0x0A, 0x21, 0x42, 0xAF, 0x9C,
};


//lint --e{818} suppress "argument "context" is not used in the sample provided"
static void optiga_lib_callback(void * context, optiga_lib_status_t return_status)
{
    optiga_lib_status = return_status;
    if (NULL != context)
    {
        // callback to upper layer here
    }
}

void example_performance_measurement(uint32_t* time_value, uint8_t time_reset_flag)
{
    if(TRUE == time_reset_flag)
    {
        *time_value = pal_os_timer_get_time_in_milliseconds();
    }
    else if(FALSE == time_reset_flag)
    {
        *time_value = pal_os_timer_get_time_in_milliseconds() - *time_value;
    }
}

/**
 * The below example demonstrates hibernate and restore functionalities
 *
 * Example for #optiga_util_open_application and #optiga_util_close_application
 *
 */
static void example_optiga_util_hibernate_restore(void)
{
    optiga_util_t * me_util = NULL;
    optiga_crypt_t * me_crypt = NULL;
    // To store the public key generated
    uint8_t public_key [100];
    //To store the signature generated
    uint8_t signature [80];
    uint16_t signature_length = sizeof(signature);
    uint16_t bytes_to_read = 1;
    optiga_key_id_t optiga_key_id;
    optiga_lib_status_t return_status = !OPTIGA_LIB_SUCCESS;
    uint8_t security_event_counter = 0;
    public_key_from_host_t public_key_details;
    //To store the generated public key as part of Generate key pair
    uint16_t public_key_length = sizeof(public_key);
    uint32_t time_taken = 0;

    OPTIGA_EXAMPLE_LOG_MESSAGE("Begin demonstrating hibernate feature...\n");    
    OPTIGA_EXAMPLE_LOG_MESSAGE(__FUNCTION__);

    do
    {
        OPTIGA_EXAMPLE_LOG_MESSAGE("Create both crypt and util instances\n")
        //Create an instance of optiga_util and optiga_crypt
        me_util = optiga_util_create(0, optiga_lib_callback, NULL);
        if (NULL == me_util)
        {
            break;
        }

        me_crypt = optiga_crypt_create(0, optiga_lib_callback, NULL);
        if (NULL == me_crypt)
        {
            break;
        }

        /**
         * 1. Open the application on OPTIGA which is a pre-condition to perform any other operations
         *    using optiga_util_open_application
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("1. Open the application on OPTIGA which is a pre-condition to perform any other operations\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
                
        START_PERFORMANCE_MEASUREMENT(time_taken);
        
        return_status = optiga_util_open_application(me_util, 0);

        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * 2. Generate ECC Key pair
         *       - Use ECC NIST P 256 Curve
         *       - Specify the Key Usage (Key Agreement or Sign based on requirement)
         *       - Store the Private key generated in a Session OID
         *       - Export Public Key
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("2. Generate ECC Key pair\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        optiga_key_id = OPTIGA_KEY_ID_SESSION_BASED;
        return_status = optiga_crypt_ecc_generate_keypair(me_crypt,
                                                          OPTIGA_ECC_CURVE_NIST_P_256,
                                                          (uint8_t)OPTIGA_KEY_USAGE_SIGN,
                                                          FALSE,
                                                          &optiga_key_id,
                                                          public_key,
                                                          &public_key_length);
        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * - The subsequent steps will hibernate OPTIGA and save the session and optiga comms related information in OPTIGA .
         * - The session and optiga comms related information are then restored back and crypto operation are performed using these information.
         */

        /**
         * 3. To perform the hibernate, Security Event Counter(SEC) must be 0.
         *    Read SEC data object (0xE0C5) and wait until SEC = 0
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("Note: The subsequent steps will hibernate OPTIGA and save the session and optiga comms related information in OPTIGA\n")
        OPTIGA_EXAMPLE_LOG_MESSAGE("The session and optiga comms related information are then restored back and crypto operation are performed using these information.\n")
        OPTIGA_EXAMPLE_LOG_MESSAGE("3. To perform the hibernate, Security Event Counter(SEC) must be 0.\n")
        OPTIGA_EXAMPLE_LOG_MESSAGE("Read SEC data object (0xE0C5) and wait until SEC = 0.\n")
        do
        {
            optiga_lib_status = OPTIGA_LIB_BUSY;
            return_status = optiga_util_read_data(me_util,
                                                  0xE0C5,
                                                  0x0000,
                                                  &security_event_counter,
                                                  &bytes_to_read);

            WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);
            pal_os_timer_delay_in_milliseconds(1000);
        } while (0 != security_event_counter);

        /**
         * 4. Hibernate the application on OPTIGA
         *    using optiga_util_close_application with perform_hibernate parameter as true
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("4. Hibernate the application on OPTIGA.\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        return_status = optiga_util_close_application(me_util, 1);

        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * 5. Restore the application on OPTIGA
         *    using optiga_util_open_application with perform_restore parameter as true
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("5. Restore the application on OPTIGA.\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        return_status = optiga_util_open_application(me_util, 1);

        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * 6. Sign the digest using the session key from Step 3
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("6. Sign the digest using the session key from Step 3.\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        return_status = optiga_crypt_ecdsa_sign(me_crypt,
                                                digest,
                                                sizeof(digest),
                                                OPTIGA_KEY_ID_SESSION_BASED,
                                                signature,
                                                &signature_length);

        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * 7. Verify ECDSA signature using public key from step 2
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("7. Verify ECDSA signature using public key from step 2.\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        public_key_details.public_key = public_key;
        public_key_details.length = public_key_length;
        public_key_details.key_type = (uint8_t)OPTIGA_ECC_CURVE_NIST_P_256;
        return_status = optiga_crypt_ecdsa_verify (me_crypt,
                                                   digest,
                                                   sizeof(digest),
                                                   signature,
                                                   signature_length,
                                                   OPTIGA_CRYPT_HOST_DATA,
                                                   &public_key_details);

        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);

        /**
         * 8. Close the application on OPTIGA without hibernating
         *    using optiga_util_close_application
         */
        OPTIGA_EXAMPLE_LOG_MESSAGE("8. Close the application on OPTIGA without hibernating.\n")
        optiga_lib_status = OPTIGA_LIB_BUSY;
        return_status = optiga_util_close_application(me_util, 0);
        WAIT_AND_CHECK_STATUS(return_status, optiga_lib_status);
        
        READ_PERFORMANCE_MEASUREMENT(time_taken);
        
        return_status = OPTIGA_LIB_SUCCESS;
        OPTIGA_EXAMPLE_LOG_MESSAGE("Hibernate feature demonstration completed...\n");
    } while (FALSE);
    OPTIGA_EXAMPLE_LOG_STATUS(return_status);

    OPTIGA_EXAMPLE_LOG_MESSAGE("Destroy both crypt and util instances.\n")

    if (me_util)
    {
        //Destroy the instance after the completion of usecase if not required.
        return_status = optiga_util_destroy(me_util);
        if(OPTIGA_LIB_SUCCESS != return_status)
        {
            //lint --e{774} suppress This is a generic macro
            OPTIGA_EXAMPLE_LOG_STATUS(return_status);
        }
    }

    if (me_crypt)
    {
        //Destroy the instance after the completion of usecase if not required.
        return_status = optiga_crypt_destroy(me_crypt);
        if(OPTIGA_LIB_SUCCESS != return_status)
        {
            //lint --e{774} suppress This is a generic macro
            OPTIGA_EXAMPLE_LOG_STATUS(return_status);
        }
    }
}

extern void example_optiga_crypt_hash(void);
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM4 CPU. It sets up a timer to trigger a 
* periodic interrupt. The main while loop checks for the status of a flag set 
* by the interrupt and toggles an LED at 1Hz to create an LED blinky. The 
* while loop also checks whether the 'Enter' key was pressed and 
* stops/restarts LED blinking.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    
    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("****************** OPTIGA™ Trust M: Power management Example ****************** \r\n\n");

#ifdef OPTIGA_TRUSTM_VDD
    example_optiga_util_hibernate_restore();
#else
    OPTIGA_EXAMPLE_LOG_MESSAGE("If you see this message, it means that your board doesn't implement the OPTIGA VCC Control circuit.\n");
    OPTIGA_EXAMPLE_LOG_MESSAGE("If you have a custom board please define the CYBSP_TRUSTM_VDD macro with the corresponding GPIO in your build.\n");
#endif

}

/* [] END OF FILE */
