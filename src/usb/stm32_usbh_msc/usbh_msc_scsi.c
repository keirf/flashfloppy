/**
******************************************************************************
* @file    usbh_msc_scsi.c
* @author  MCD Application Team
* @version V2.2.0
* @date    09-November-2015
* @brief   This file implements the SCSI commands
******************************************************************************
* @attention
*
* <h2><center>&copy; COPYRIGHT 2015 STMicroelectronics</center></h2>
*
* Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
* You may not use this file except in compliance with the License.
* You may obtain a copy of the License at:
*
*        http://www.st.com/software_license_agreement_liberty_v2
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
******************************************************************************
*/

#include "usbh_msc_core.h"
#include "usbh_msc_scsi.h"
#include "usbh_msc_bot.h"
#include "usbh_ioreq.h"
#include "usbh_def.h"

MassStorageParameter_TypeDef USBH_MSC_Param;

/**
 * @brief  USBH_MSC_TestUnitReady
 *         Issues 'Test unit ready' command to the device. Once the response
 *         received, it updates the status to upper layer.
 * @param  None
 * @retval Status
 */
uint8_t USBH_MSC_TestUnitReady (USB_OTG_CORE_HANDLE *pdev)
{
    USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:
            /*Prepare the CBW and relevent field*/
            USBH_MSC_CBWData.field.CBWTransferLength = 0;       /* No Data Transfer */
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_OUT;
            USBH_MSC_CBWData.field.CBWLength = 6;
            USBH_MSC_BOTXferParam.pRxTxBuff = USBH_MSC_CSWData.CSWArray;
            USBH_MSC_BOTXferParam.DataLength = USBH_MSC_CSW_MAX_LENGTH;
            USBH_MSC_BOTXferParam.MSCStateCurrent = USBH_MSC_TEST_UNIT_READY;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0] = 0x00;
            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;
            /* Start the transfer, then let the state
               machine magage the other transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;
            break;

        case CMD_WAIT_STATUS:
            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;

                status = USBH_MSC_OK;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }

            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }
            break;

        default:
            break;
        }
    }
    return status;
}


/**
 * @brief  USBH_MSC_ReadCapacity10
 *         Issue the read capacity command to the device. Once the response
 *         received, it updates the status to upper layer
 * @param  None
 * @retval Status
 */
uint8_t USBH_MSC_ReadCapacity10(USB_OTG_CORE_HANDLE *pdev)
{
    USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:
            /*Prepare the CBW and relevent field*/
            USBH_MSC_CBWData.field.CBWTransferLength = 8;
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_IN;
            USBH_MSC_CBWData.field.CBWLength = 10;

            USBH_MSC_BOTXferParam.pRxTxBuff = Cfg_Rx_Buffer;
            USBH_MSC_BOTXferParam.MSCStateCurrent = USBH_MSC_READ_CAPACITY10;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0] = 0x25;
            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;

            /* Start the transfer, then let the state machine manage the other
               transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;
            break;

        case CMD_WAIT_STATUS:
            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /*assign the capacity*/
                (((uint8_t*)&USBH_MSC_Param.MSCapacity )[3]) = Cfg_Rx_Buffer[0];
                (((uint8_t*)&USBH_MSC_Param.MSCapacity )[2]) = Cfg_Rx_Buffer[1];
                (((uint8_t*)&USBH_MSC_Param.MSCapacity )[1]) = Cfg_Rx_Buffer[2];
                (((uint8_t*)&USBH_MSC_Param.MSCapacity )[0]) = Cfg_Rx_Buffer[3];

                /*assign the page length*/
                (((uint8_t*)&USBH_MSC_Param.MSPageLength )[1]) = Cfg_Rx_Buffer[6];
                (((uint8_t*)&USBH_MSC_Param.MSPageLength )[0]) = Cfg_Rx_Buffer[7];

                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_OK;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }
            else
            {
                /* Wait for the Commands to get Completed */
                /* NO Change in state Machine */
            }
            break;

        default:
            break;
        }
    }
    return status;
}


/**
 * @brief  USBH_MSC_ModeSense6
 *         Issue the Mode Sense6 Command to the device. This function is used
 *          for reading the WriteProtect Status of the Mass-Storage device.
 * @param  None
 * @retval Status
 */
uint8_t USBH_MSC_ModeSense6(USB_OTG_CORE_HANDLE *pdev)
{
    USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:
            /*Prepare the CBW and relevent field*/
            USBH_MSC_CBWData.field.CBWTransferLength = 63;
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_IN;
            USBH_MSC_CBWData.field.CBWLength = 6;

            USBH_MSC_BOTXferParam.pRxTxBuff = Cfg_Rx_Buffer;
            USBH_MSC_BOTXferParam.MSCStateCurrent = USBH_MSC_MODE_SENSE6;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0]  = 0x1a; /* ModeSense6 */
            USBH_MSC_CBWData.field.CBWCB[2]  = 0x3f; /* All pages */
            USBH_MSC_CBWData.field.CBWCB[4]  = 63;

            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;

            /* Start the transfer, then let the state machine manage the other
               transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;
            break;

        case CMD_WAIT_STATUS:
            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /* Assign the Write Protect status */
                /* If WriteProtect = 0, Writing is allowed
                   If WriteProtect != 0, Disk is Write Protected */
                USBH_MSC_Param.MSWriteProtect = !!(Cfg_Rx_Buffer[2] & 0x80);

                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_OK;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }
            else
            {
                /* Wait for the Commands to get Completed */
                /* NO Change in state Machine */
            }
            break;

        default:
            break;
        }
    }
    return status;
}

/**
 * @brief  USBH_MSC_RequestSense
 *         Issues the Request Sense command to the device. Once the response
 *         received, it updates the status to upper layer
 * @param  None
 * @retval Status
 */
uint8_t USBH_MSC_RequestSense(USB_OTG_CORE_HANDLE *pdev)
{
    USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:

            /*Prepare the CBW and relevant field*/
            USBH_MSC_CBWData.field.CBWTransferLength = 63;
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_IN;
            USBH_MSC_CBWData.field.CBWLength = 6;
            USBH_MSC_BOTXferParam.pRxTxBuff = Cfg_Rx_Buffer;
            USBH_MSC_BOTXferParam.MSCStateBkp = USBH_MSC_BOTXferParam.MSCStateCurrent;
            USBH_MSC_BOTXferParam.MSCStateCurrent = USBH_MSC_REQUEST_SENSE;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0]  = 0x03;
            USBH_MSC_CBWData.field.CBWCB[4]  = 63;

            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;
            /* Start the transfer, then let the state machine manage
               the other transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;

            break;

        case CMD_WAIT_STATUS:

            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /* Get Sense data*/
                (((uint8_t*)&USBH_MSC_Param.MSSenseKey )[3]) = Cfg_Rx_Buffer[0];
                (((uint8_t*)&USBH_MSC_Param.MSSenseKey )[2]) = Cfg_Rx_Buffer[1];
                (((uint8_t*)&USBH_MSC_Param.MSSenseKey )[1]) = Cfg_Rx_Buffer[2];
                (((uint8_t*)&USBH_MSC_Param.MSSenseKey )[0]) = Cfg_Rx_Buffer[3];

                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_OK;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }

            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }

            else
            {
                /* Wait for the Commands to get Completed */
                /* NO Change in state Machine */
            }
            break;

        default:
            break;
        }
    }
    return status;
}


/**
 * @brief  USBH_MSC_Write10
 *         Issue the write command to the device. Once the response received,
 *         it updates the status to upper layer
 * @param  dataBuffer : DataBuffer contains the data to write
 * @param  address : Address to which the data will be written
 * @param  nbOfbytes : NbOfbytes to be written
 * @retval Status
 */
uint8_t USBH_MSC_Write10(USB_OTG_CORE_HANDLE *pdev,
                         uint8_t *dataBuffer,
                         uint32_t address,
                         uint32_t nbOfbytes)
{
    USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;
    uint16_t nbOfPages;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:
            USBH_MSC_CBWData.field.CBWTransferLength = nbOfbytes;
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_OUT;
            USBH_MSC_CBWData.field.CBWLength = 10;
            USBH_MSC_BOTXferParam.pRxTxBuff = dataBuffer;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0]  = 0x2a;

            /*logical block address*/
            USBH_MSC_CBWData.field.CBWCB[2]  = (((uint8_t*)&address)[3]) ;
            USBH_MSC_CBWData.field.CBWCB[3]  = (((uint8_t*)&address)[2]);
            USBH_MSC_CBWData.field.CBWCB[4]  = (((uint8_t*)&address)[1]);
            USBH_MSC_CBWData.field.CBWCB[5]  = (((uint8_t*)&address)[0]);

            /*USBH_MSC_PAGE_LENGTH = 512*/
            nbOfPages = nbOfbytes/ USBH_MSC_PAGE_LENGTH;

            /*Transfer length */
            USBH_MSC_CBWData.field.CBWCB[7]  = (((uint8_t *)&nbOfPages)[1]) ;
            USBH_MSC_CBWData.field.CBWCB[8]  = (((uint8_t *)&nbOfPages)[0]) ;

            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;
            /* Start the transfer, then let the state machine
               manage the other transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;

            break;

        case CMD_WAIT_STATUS:
            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_OK;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }
            else if ( USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }
            break;

        default:
            break;
        }
    }
    return status;
}

/**
 * @brief  USBH_MSC_Read10
 *         Issue the read command to the device. Once the response received,
 *         it updates the status to upper layer
 * @param  dataBuffer : DataBuffer will contain the data to be read
 * @param  address : Address from which the data will be read
 * @param  nbOfbytes : NbOfbytes to be read
 * @retval Status
 */
uint8_t USBH_MSC_Read10(USB_OTG_CORE_HANDLE *pdev,
                        uint8_t *dataBuffer,
                        uint32_t address,
                        uint32_t nbOfbytes)
{
    static USBH_MSC_Status_TypeDef status = USBH_MSC_BUSY;
    uint16_t nbOfPages;
    status = USBH_MSC_BUSY;

    if(HCD_IsDeviceConnected(pdev))
    {
        switch(USBH_MSC_BOTXferParam.CmdStateMachine)
        {
        case CMD_SEND_STATE:
            /*Prepare the CBW and relevant field*/
            USBH_MSC_CBWData.field.CBWTransferLength = nbOfbytes;
            USBH_MSC_CBWData.field.CBWFlags = USB_EP_DIR_IN;
            USBH_MSC_CBWData.field.CBWLength = 10;

            USBH_MSC_BOTXferParam.pRxTxBuff = dataBuffer;

            memset(USBH_MSC_CBWData.field.CBWCB, 0,
                   sizeof(USBH_MSC_CBWData.field.CBWCB));

            USBH_MSC_CBWData.field.CBWCB[0]  = 0x28;

            /*logical block address*/

            USBH_MSC_CBWData.field.CBWCB[2]  = (((uint8_t*)&address)[3]);
            USBH_MSC_CBWData.field.CBWCB[3]  = (((uint8_t*)&address)[2]);
            USBH_MSC_CBWData.field.CBWCB[4]  = (((uint8_t*)&address)[1]);
            USBH_MSC_CBWData.field.CBWCB[5]  = (((uint8_t*)&address)[0]);

            /*USBH_MSC_PAGE_LENGTH = 512*/
            nbOfPages = nbOfbytes/ USBH_MSC_PAGE_LENGTH;

            /*Transfer length */
            USBH_MSC_CBWData.field.CBWCB[7]  = (((uint8_t *)&nbOfPages)[1]) ;
            USBH_MSC_CBWData.field.CBWCB[8]  = (((uint8_t *)&nbOfPages)[0]) ;


            USBH_MSC_BOTXferParam.BOTState = USBH_MSC_SEND_CBW;
            /* Start the transfer, then let the state machine
               manage the other transactions */
            USBH_MSC_BOTXferParam.MSCState = USBH_MSC_BOT_USB_TRANSFERS;
            USBH_MSC_BOTXferParam.BOTXferStatus = USBH_MSC_BUSY;
            USBH_MSC_BOTXferParam.CmdStateMachine = CMD_WAIT_STATUS;

            status = USBH_MSC_BUSY;

            break;

        case CMD_WAIT_STATUS:

            if(USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_OK)
            {
                /* Commands successfully sent and Response Received  */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_OK;
            }
            else if (USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_FAIL)
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_FAIL;
            }
            else if (USBH_MSC_BOTXferParam.BOTXferStatus == USBH_MSC_PHASE_ERROR )
            {
                /* Failure Mode */
                USBH_MSC_BOTXferParam.CmdStateMachine = CMD_SEND_STATE;
                status = USBH_MSC_PHASE_ERROR;
            }
            break;

        default:
            break;
        }
    }
    return status;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
