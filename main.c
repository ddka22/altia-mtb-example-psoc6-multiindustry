/*******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the Altia Multi-Industry Demo
 *              for ModusToolbox.
 *
 * Related Document: See README.md
 *
 *
 ********************************************************************************
 * Copyright 2021-2023, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

/*******************************************************************************
 * Header Files
 *******************************************************************************/
#include "cyhal.h"
#include "cybsp.h"

#include "st7789.h"
#include "TFT.h"

#include "cy8ckit_028_tft_pins.h"
#include "cy_retarget_io.h"

#include "miniGL.h"
#include "miniGL_software_render.h"

/*******************************************************************************
 * Macros
 *******************************************************************************/
#define GPIO_ISR_FLAG           (1u)
#define GPIO_INTERRUPT_PRIORITY (2u)
#define BTN1_CB_ARG             (0)
#define BTN2_CB_ARG             (1)

/*******************************************************************************
 * Global Variables
 *******************************************************************************/
/* The pins above are defined by the CY8CKIT-028-TFT library. If the display is being used on different hardware the mappings will be different. */
const mtb_st7789v_pins_t tft_pins =
{
        .db08 = CY8CKIT_028_TFT_PIN_DISPLAY_DB8,
        .db09 = CY8CKIT_028_TFT_PIN_DISPLAY_DB9,
        .db10 = CY8CKIT_028_TFT_PIN_DISPLAY_DB10,
        .db11 = CY8CKIT_028_TFT_PIN_DISPLAY_DB11,
        .db12 = CY8CKIT_028_TFT_PIN_DISPLAY_DB12,
        .db13 = CY8CKIT_028_TFT_PIN_DISPLAY_DB13,
        .db14 = CY8CKIT_028_TFT_PIN_DISPLAY_DB14,
        .db15 = CY8CKIT_028_TFT_PIN_DISPLAY_DB15,
        .nrd  = CY8CKIT_028_TFT_PIN_DISPLAY_NRD,
        .nwr  = CY8CKIT_028_TFT_PIN_DISPLAY_NWR,
        .dc   = CY8CKIT_028_TFT_PIN_DISPLAY_DC,
        .rst  = CY8CKIT_028_TFT_PIN_DISPLAY_RST
};

cy_stc_scb_uart_context_t debug_uart_context;

/* Callback data for button interrupts */
cyhal_gpio_callback_data_t btn1_cb_data;
cyhal_gpio_callback_data_t btn2_cb_data;

/* Altia Draw Buffer */
MINIGL_INT16 backBuffer[320 * 240] = {0};

/* Altia BAM (Binary Asset Manager) addresses of binary graphic assets */
extern MINIGL_UINT32 __altia_table_start__;
MINIGL_UINT8 *altiaTable = (MINIGL_UINT8 *)&__altia_table_start__;
extern MINIGL_UINT32 __altia_images_start__;
MINIGL_UINT8 *altiaImages = (MINIGL_UINT8 *)&__altia_images_start__;
extern MINIGL_UINT32 __altia_fonts_start__;
MINIGL_UINT8 *altiaFonts = (MINIGL_UINT8 *)&__altia_fonts_start__;

/* Global application variables */
MINIGL_UINT8 global_jump_to_screen = 0;
MINIGL_UINT8 current_screen = 0;
MINIGL_UINT8 screenChange = 0;
MINIGL_UINT8 aboutTrigger = 0;
MINIGL_UINT8 aboutScreenShown = 0;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
void gpio_interrupt_handler(void* handler_arg, cyhal_gpio_event_t event);

/*******************************************************************************
 * Function Definitions
 *******************************************************************************/

/**********************************************************************
 * Altia miniGL BSP functions (overwriting WEAK implementations) START *
 **********************************************************************/

void *bsp_GetBackFrameBuffer()
{
    return backBuffer;
}

MINIGL_INT bsp_DriverFlushed(MINIGL_CONST MINIGL_INT16 dirtyExtentCount, MINIGL_CONST BSP_CLIPPING_RECTANGLE *dirtyExtentList)
{
    MINIGL_INT16 extentCount;

    for(extentCount = 0; extentCount < dirtyExtentCount; extentCount++)
    {
        TFT_UpdateArea(dirtyExtentList[extentCount].x0, dirtyExtentList[extentCount].y0, dirtyExtentList[extentCount].x1, dirtyExtentList[extentCount].y1, backBuffer);
    }

    return 0;
}

MINIGL_UINT8 *bsp_ReflashQueryString(MINIGL_CONST MINIGL_UINT16 *string)
{
    MINIGL_CONST MINIGL_UINT8 *address = NULL;

    char local_string[128] = {0};
    MINIGL_UINT32 count = 0;

    if(string == NULL)
    {
        return altiaTable;
    }

    while((count < 128) && (string[count] != 0x0000))
    {
        local_string[count] = (MINIGL_UINT8)string[count];
        count++;
    }

    if(0 == strcmp(local_string, "\\images"))
    {
        address = altiaImages;
    }
    else if(0 == strcmp(local_string, "\\fonts"))
    {
        address = altiaFonts;
    }

    return (MINIGL_UINT8 *)address;
}

/********************************************************************
 * Altia miniGL BSP functions (overwriting WEAK implementations) END *
 ********************************************************************/

/* Handling Interrupts of USER BNT1 and USER BTN2 */
void gpio_interrupt_handler(void* handler_arg, cyhal_gpio_event_t event)
{
    /* Check to determine which pin triggered the interrupt */
    uint32_t interrupt_cause = *((uint32_t *) handler_arg);

    if((interrupt_cause == BTN1_CB_ARG) && (event == CYHAL_GPIO_IRQ_FALL))
    {
        screenChange = GPIO_ISR_FLAG;
    }

    if((interrupt_cause == BTN2_CB_ARG) && (event == CYHAL_GPIO_IRQ_FALL))
    {
        aboutTrigger = GPIO_ISR_FLAG;
    }
}

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 * This is the main function for CPU. It
 *    1. Initializes the display controllers
 *    2. Uses the Altia APIs to transition between different screens
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;

#if defined (CY_DEVICE_SECURE)
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    result = cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    cyhal_wdt_free(&wdt_obj);
#endif

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the retarget-io */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    /* Initialize user buttons */
    cyhal_gpio_init(CYBSP_USER_BTN1, CYHAL_GPIO_DIR_INPUT, CYBSP_USER_BTN_DRIVE, CYBSP_BTN_OFF);
    cyhal_gpio_init(CYBSP_USER_BTN2, CYHAL_GPIO_DIR_INPUT, CYBSP_USER_BTN_DRIVE, CYBSP_BTN_OFF);

    uint32_t btn1_arg = BTN1_CB_ARG;
    uint32_t btn2_arg = BTN2_CB_ARG;

    btn1_cb_data.callback = gpio_interrupt_handler;
    btn1_cb_data.callback_arg = (void *) &btn1_arg;

    btn2_cb_data.callback = gpio_interrupt_handler;
    btn2_cb_data.callback_arg = (void *) &btn2_arg;

    /* Configure GPIO pin to generate interrupts */
    cyhal_gpio_register_callback(CYBSP_USER_BTN1, &btn1_cb_data);
    cyhal_gpio_register_callback(CYBSP_USER_BTN2, &btn2_cb_data);

    /* Enable GPIO interrupts for falling edge */
    cyhal_gpio_enable_event(CYBSP_USER_BTN1, CYHAL_GPIO_IRQ_FALL, GPIO_INTERRUPT_PRIORITY, true);
    cyhal_gpio_enable_event(CYBSP_USER_BTN2, CYHAL_GPIO_IRQ_FALL, GPIO_INTERRUPT_PRIORITY, true);

    /* Initialize the display controller */
    result = st7789_init8(&tft_pins);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    TFT_HwReset();
    TFT_SwReset();
    TFT_ConfigureRotation(0);
    TFT_DisplayOn();
    TFT_FillScreen(BLACK);

    printf("Altia Multi-Industry Demo!\n\r");

    /* initialize Altia GUI */
    if(altiaInitDriver() < 0)
    {
        printf("altiaInitDriver() failed!\n\r");

        /* stop here */
        while(1);
    }
    else
    {
        printf("altiaInitDriver() Ok!\n\r");
    }

    altiaFlushOutput();

    for (;;)
    {
        // change screen (USER BTN1 released)
        if(screenChange)
        {
            if(global_jump_to_screen == 0) //
            {
                altiaSendEvent(ALT_ANIM(INTRO_to_main), 1);

                global_jump_to_screen = 1;
            }
            else
            {
                AltiaEventType value;
                altiaPollEvent(ALT_ANIM(GLOBAL_demo_type), &value);

                global_jump_to_screen = (int)value + 1;

                if(global_jump_to_screen > 4) global_jump_to_screen = 1;
            }

            // make sure that an eventually running About Screen is set invisible
            altiaSendEvent(ALT_ANIM(ABOUT_screen_visible), 0);
            altiaSendEvent(ALT_ANIM(GLOBAL_jump_to_screen), global_jump_to_screen);

            // when a screen change was done ABOUT screen is not shown
            aboutScreenShown = 0;

            screenChange = 0;
        }

        // ABOUT screen (USER BTN2 released)
        if(aboutTrigger)
        {
            if(!aboutScreenShown)
            {
                AltiaEventType value;

                // check if INTRO is still running
                altiaPollEvent(ALT_ANIM(INTRO_screen_visible), &value);

                if(value == 1)
                {
                    // do nothing
                }
                else
                {
                    // figure out which demo screen is currently shown
                    altiaPollEvent(ALT_ANIM(GLOBAL_demo_type), &value);

                    if(value == 1)
                    {
                        altiaSendEvent(ALT_ANIM(MEDICAL_stimulus_about), 1);
                        aboutScreenShown = 1;
                    }
                    else if(value == 2)
                    {
                        altiaSendEvent(ALT_ANIM(HOMEAPP_stimulus_about), 1);
                        aboutScreenShown = 1;
                    }
                    else if(value == 3)
                    {
                        altiaSendEvent(ALT_ANIM(CONAG_stimulus_about), 1);
                        aboutScreenShown = 1;
                    }
                    else if(value == 4)
                    {
                        altiaSendEvent(ALT_ANIM(AUTO_stimulus_about), 1);
                        aboutScreenShown = 1;
                    }
                }
            }
            else
            {
                altiaSendEvent(ALT_ANIM(ABOUT_stimulus_back), 1);
                aboutScreenShown = 0;
            }

            aboutTrigger = 0;
        }

        altiaSendEvent(ALT_ANIM(do_timer), 1);

        altiaFlushOutput();
    }
}

/* [] END OF FILE */
