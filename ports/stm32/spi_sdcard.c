// ------------------------------------------------------------------------------------------------
// includes
// ------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "lib/oofatfs/ff.h"
#include "extmod/vfs_fat.h"

#include "spi_sdcard.h"
#include "sdcard.h"
#include "pin.h"
#include "pin_static_af.h"
#include "bufhelper.h"

#include "stm32wbxx_nucleo.h"
#include "spi_sd_hal.h"

#if MICROPY_HW_ENABLE_SPI_SDCARD

#if defined(STM32WB)

// ------------------------------------------------------------------------------------------------
// private constant macros
// ------------------------------------------------------------------------------------------------


#define SD_BLOCK_SIZE                                   (512)

// ------------------------------------------------------------------------------------------------
// private function macros
// ------------------------------------------------------------------------------------------------

typedef enum
{
    SD_SUCCESS                 = HAL_OK,              /**< Result is a success. */
    SD_INVALID_PARAMETER,                                                 /**< Result is a failure: invalid parameter. */
    SD_NOT_INITIALIZED,                                                   /**< Result is a failure: uninitialized. */
    SD_ALREADY_INITIALIZED,                                               /**< Result is a failure: initialized. */

    // start of specific errors (starting from -128)
    SD_WRITE_ERROR                   = -128,                            /**< Result is an error. */
    SD_READ_ERROR                    = -129,                            /**< Result is an error. */
    SD_ERASE_ERROR                   = -130,                            /**< Result is an error. */
    SD_FAILED                            = -131,                            /**< Result is an error. */
    SD_TIMEOUT_ERROR                 = -132,                            /**< Result is an error. */
} sd_result_t;

typedef SD_CardInfo sd_info_t;

// ------------------------------------------------------------------------------------------------
// private typedefs, structures, unions and enums
// ------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// private variables
// ------------------------------------------------------------------------------------------------

STATIC sd_info_t sd_handle = { 0 };
STATIC uint8_t sd_buffer_rw[SD_BLOCK_SIZE] = { 0 };
STATIC volatile uint16_t flag_SDHC = 0;

const mp_obj_base_t pyb_sdcard_obj = {&pyb_sdcard_type};
// ------------------------------------------------------------------------------------------------
// public variables
// ------------------------------------------------------------------------------------------------

extern void                      SD_IO_Init(void);
extern void                      SD_IO_CSState(uint8_t state);
extern void                      SD_IO_WriteReadData(const uint8_t *DataIn, uint8_t *DataOut, uint16_t DataLength);
extern uint8_t                   SD_IO_WriteByte(uint8_t Data);

// ------------------------------------------------------------------------------------------------
// private function prototypes
// ------------------------------------------------------------------------------------------------

STATIC uint8_t SD_ReadData(void);
STATIC sd_result_t SD_WaitData(uint8_t data);
STATIC uint8_t SD_GetDataResponse(void);
STATIC sd_result_t SD_GoIdleState(void);
STATIC void    SD_SendCmd(uint8_t Cmd, uint32_t Arg, uint8_t Crc, uint8_t Answer, SD_CmdAnswer_typedef *response);
STATIC sd_result_t SD_GetCSDRegister(SD_CSD *Csd);
STATIC sd_result_t SD_GetCIDRegister(SD_CID *Cid);

// ------------------------------------------------------------------------------------------------
// private functions
// ------------------------------------------------------------------------------------------------

STATIC uint8_t SD_ReadData(void) {
    uint8_t timeout = 0x08;
    uint8_t readvalue;

    /* Check if response is got or a timeout is happen */
    do
    {
        readvalue = SD_IO_WriteByte(SD_DUMMY_BYTE);
        timeout--;
    }
    while ((readvalue == SD_DUMMY_BYTE) && timeout);

    /* Right response got */
    return readvalue;
}

STATIC sd_result_t SD_WaitData(uint8_t data) {
    uint16_t timeout = 0xFFFF;
    uint8_t readvalue;

    /* Check if response is got or a timeout is happen */

    do
    {
        readvalue = SD_IO_WriteByte(SD_DUMMY_BYTE);
        timeout--;
    }
    while ((readvalue != data) && timeout);

    if (timeout == 0) {
        /* After time out */
        return SD_TIMEOUT_ERROR;
    }

    /* Right response got */
    return SD_SUCCESS;
}

STATIC uint8_t SD_GetDataResponse(void) {
    uint8_t dataresponse;
    uint8_t rvalue = SD_DATA_OTHER_ERROR;

    dataresponse = SD_IO_WriteByte(SD_DUMMY_BYTE);
    SD_IO_WriteByte(SD_DUMMY_BYTE);     /* read the busy response byte*/

    /* Mask unused bits */
    switch (dataresponse & 0x1F)
    {
        case SD_DATA_OK:
            rvalue = SD_DATA_OK;

            /* Set CS High */
            SD_IO_CSState(1);
            /* Set CS Low */
            SD_IO_CSState(0);

            /* Wait IO line return 0xFF */
            while (SD_IO_WriteByte(SD_DUMMY_BYTE) != 0xFF) {
                ;
            }
            break;
        case SD_DATA_CRC_ERROR:
            rvalue = SD_DATA_CRC_ERROR;
            break;
        case SD_DATA_WRITE_ERROR:
            rvalue = SD_DATA_WRITE_ERROR;
            break;
        default:
            break;
    }

    /* Return response */
    return rvalue;
}

STATIC sd_result_t SD_GoIdleState(void) {
    SD_CmdAnswer_typedef response;
    __IO uint8_t counter = 0;
    /* Send CMD0 (SD_CMD_GO_IDLE_STATE) to put SD in SPI mode and
    wait for In Idle State Response (R1 Format) equal to 0x01 */
    do {
        counter++;
        SD_SendCmd(SD_CMD_GO_IDLE_STATE, 0, 0x95, SD_ANSWER_R1_EXPECTED, &response);
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
        if (counter >= SD_MAX_TRY) {
            printf("SD Idle State ERROR\n");
            return SD_FAILED;
        }
    }
    while (response.r1 != SD_R1_IN_IDLE_STATE);

    /* Send CMD8 (SD_CMD_SEND_IF_COND) to check the power supply status
    and wait until response (R7 Format) equal to 0xAA and */
    SD_SendCmd(SD_CMD_SEND_IF_COND, 0x1AA, 0x87, SD_ANSWER_R7_EXPECTED, &response);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if ((response.r1 & SD_R1_ILLEGAL_COMMAND) == SD_R1_ILLEGAL_COMMAND) {
        /* initialise card V1 */
        do
        {
            /* initialise card V1 */
            /* Send CMD55 (SD_CMD_APP_CMD) before any ACMD command: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_APP_CMD, 0x00000000, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /* Send ACMD41 (SD_CMD_SD_APP_OP_COND) to initialize SDHC or SDXC cards: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_SD_APP_OP_COND, 0x00000000, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        }
        while (response.r1 == SD_R1_IN_IDLE_STATE);
        flag_SDHC = 0;
    } else if (response.r1 == SD_R1_IN_IDLE_STATE) {
        /* initialise card V2 */
        do {

            /* Send CMD55 (SD_CMD_APP_CMD) before any ACMD command: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_APP_CMD, 0, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /* Send ACMD41 (SD_CMD_SD_APP_OP_COND) to initialize SDHC or SDXC cards: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_SD_APP_OP_COND, 0x40000000, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        }
        while (response.r1 == SD_R1_IN_IDLE_STATE);

        if ((response.r1 & SD_R1_ILLEGAL_COMMAND) == SD_R1_ILLEGAL_COMMAND) {
            do {
                /* Send CMD55 (SD_CMD_APP_CMD) before any ACMD command: R1 response (0x00: no errors) */
                SD_SendCmd(SD_CMD_APP_CMD, 0, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
                SD_IO_CSState(1);
                SD_IO_WriteByte(SD_DUMMY_BYTE);
                if (response.r1 != SD_R1_IN_IDLE_STATE) {
                    printf("SD Idle State ERROR\n");
                    return SD_FAILED;
                }
                /* Send ACMD41 (SD_CMD_SD_APP_OP_COND) to initialize SDHC or SDXC cards: R1 response (0x00: no errors) */
                SD_SendCmd(SD_CMD_SD_APP_OP_COND, 0x00000000, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
                SD_IO_CSState(1);
                SD_IO_WriteByte(SD_DUMMY_BYTE);
            }
            while (response.r1 == SD_R1_IN_IDLE_STATE);
        }

        /* Send CMD58 (SD_CMD_READ_OCR) to initialize SDHC or SDXC cards: R3 response (0x00: no errors) */
        SD_SendCmd(SD_CMD_READ_OCR, 0x00000000, 0xFF, SD_ANSWER_R3_EXPECTED, &response);
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
        if (response.r1 != SD_R1_NO_ERROR) {
            printf("SD Idle State ERROR\n");
            return SD_FAILED;
        }
        flag_SDHC = (response.r2 & 0x40) >> 6;
    } else {
        printf("SD Idle State ERROR\n");
        return SD_FAILED;
    }
    return SD_SUCCESS;
}

STATIC void SD_SendCmd(uint8_t Cmd, uint32_t Arg, uint8_t Crc, uint8_t Answer, SD_CmdAnswer_typedef *response) {
    uint8_t frame[SD_CMD_LENGTH], frameout[SD_CMD_LENGTH];

    response->r1 = 0xFF;
    response->r2 = 0xFF;
    response->r3 = 0xFF;
    response->r4 = 0xFF;


    /* R1 Lenght = NCS(0)+ 6 Bytes command + NCR(min1 max8) + 1 Bytes answer + NEC(0) = 15bytes */
    /* R1b identical to R1 + Busy information                                                   */
    /* R2 Lenght = NCS(0)+ 6 Bytes command + NCR(min1 max8) + 2 Bytes answer + NEC(0) = 16bytes */

    /* Prepare Frame to send */
    frame[0] = (Cmd | 0x40);             /* Construct byte 1 */
    frame[1] = (uint8_t)(Arg >> 24);     /* Construct byte 2 */
    frame[2] = (uint8_t)(Arg >> 16);     /* Construct byte 3 */
    frame[3] = (uint8_t)(Arg >> 8);      /* Construct byte 4 */
    frame[4] = (uint8_t)(Arg);           /* Construct byte 5 */
    frame[5] = (Crc | 0x01);             /* Construct byte 6 */

    /* Send the command */
    SD_IO_CSState(0);
    SD_IO_WriteReadData(frame, frameout, SD_CMD_LENGTH);     /* Send the Cmd bytes */

    switch (Answer)
    {
        case SD_ANSWER_R1_EXPECTED:
            response->r1 = SD_ReadData();
            break;
        case SD_ANSWER_R1B_EXPECTED:
            response->r1 = SD_ReadData();
            response->r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            /* Set CS High */
            SD_IO_CSState(1);
            HAL_Delay(1);
            /* Set CS Low */
            SD_IO_CSState(0);

            /* Wait IO line return 0xFF */
            while (SD_IO_WriteByte(SD_DUMMY_BYTE) != 0xFF) {
                ;
            }
            break;
        case SD_ANSWER_R2_EXPECTED:
            response->r1 = SD_ReadData();
            response->r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            break;
        case SD_ANSWER_R3_EXPECTED:
        case SD_ANSWER_R7_EXPECTED:
            response->r1 = SD_ReadData();
            response->r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            response->r3 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            response->r4 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            response->r5 = SD_IO_WriteByte(SD_DUMMY_BYTE);
            break;
        default:
            break;
    }
}


STATIC sd_result_t SD_GetCSDRegister(SD_CSD *Csd) {
    uint16_t counter = 0;
    uint8_t CSD_Tab[16];
    sd_result_t retr = SD_FAILED;
    SD_CmdAnswer_typedef response;

    /* Send CMD9 (CSD register) or CMD10(CSD register) and Wait for response in the R1 format (0x00 is no errors) */
    SD_SendCmd(SD_CMD_SEND_CSD, 0, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
    if (response.r1 == SD_R1_NO_ERROR) {
        if (SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == SD_SUCCESS) {
            for (counter = 0; counter < 16; counter++)
            {
                /* Store CSD register value on CSD_Tab */
                CSD_Tab[counter] = SD_IO_WriteByte(SD_DUMMY_BYTE);
            }

            /* Get CRC bytes (not really needed by us, but required by SD) */
            SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /*************************************************************************
        CSD header decoding
        *************************************************************************/

            /* Byte 0 */
            Csd->CSDStruct = (CSD_Tab[0] & 0xC0) >> 6;
            Csd->Reserved1 = CSD_Tab[0] & 0x3F;

            /* Byte 1 */
            Csd->TAAC = CSD_Tab[1];

            /* Byte 2 */
            Csd->NSAC = CSD_Tab[2];

            /* Byte 3 */
            Csd->MaxBusClkFrec = CSD_Tab[3];

            /* Byte 4/5 */
            Csd->CardComdClasses = (CSD_Tab[4] << 4) | ((CSD_Tab[5] & 0xF0) >> 4);
            Csd->RdBlockLen = CSD_Tab[5] & 0x0F;

            /* Byte 6 */
            Csd->PartBlockRead = (CSD_Tab[6] & 0x80) >> 7;
            Csd->WrBlockMisalign = (CSD_Tab[6] & 0x40) >> 6;
            Csd->RdBlockMisalign = (CSD_Tab[6] & 0x20) >> 5;
            Csd->DSRImpl = (CSD_Tab[6] & 0x10) >> 4;

            /*************************************************************************
        CSD v1/v2 decoding
        *************************************************************************/

            if (flag_SDHC == 0) {
                Csd->version.v1.Reserved1 = ((CSD_Tab[6] & 0x0C) >> 2);

                Csd->version.v1.DeviceSize = ((CSD_Tab[6] & 0x03) << 10)
                    | (CSD_Tab[7] << 2)
                    | ((CSD_Tab[8] & 0xC0) >> 6);
                Csd->version.v1.MaxRdCurrentVDDMin = (CSD_Tab[8] & 0x38) >> 3;
                Csd->version.v1.MaxRdCurrentVDDMax = (CSD_Tab[8] & 0x07);
                Csd->version.v1.MaxWrCurrentVDDMin = (CSD_Tab[9] & 0xE0) >> 5;
                Csd->version.v1.MaxWrCurrentVDDMax = (CSD_Tab[9] & 0x1C) >> 2;
                Csd->version.v1.DeviceSizeMul = ((CSD_Tab[9] & 0x03) << 1)
                    | ((CSD_Tab[10] & 0x80) >> 7);
            } else {
                Csd->version.v2.Reserved1 = ((CSD_Tab[6] & 0x0F) << 2) | ((CSD_Tab[7] & 0xC0) >> 6);
                Csd->version.v2.DeviceSize = ((CSD_Tab[7] & 0x3F) << 16) | (CSD_Tab[8] << 8) | CSD_Tab[9];
                Csd->version.v2.Reserved2 = ((CSD_Tab[10] & 0x80) >> 8);
            }

            Csd->EraseSingleBlockEnable = (CSD_Tab[10] & 0x40) >> 6;
            Csd->EraseSectorSize = ((CSD_Tab[10] & 0x3F) << 1)
                | ((CSD_Tab[11] & 0x80) >> 7);
            Csd->WrProtectGrSize = (CSD_Tab[11] & 0x7F);
            Csd->WrProtectGrEnable = (CSD_Tab[12] & 0x80) >> 7;
            Csd->Reserved2 = (CSD_Tab[12] & 0x60) >> 5;
            Csd->WrSpeedFact = (CSD_Tab[12] & 0x1C) >> 2;
            Csd->MaxWrBlockLen = ((CSD_Tab[12] & 0x03) << 2)
                | ((CSD_Tab[13] & 0xC0) >> 6);
            Csd->WriteBlockPartial = (CSD_Tab[13] & 0x20) >> 5;
            Csd->Reserved3 = (CSD_Tab[13] & 0x1F);
            Csd->FileFormatGrouop = (CSD_Tab[14] & 0x80) >> 7;
            Csd->CopyFlag = (CSD_Tab[14] & 0x40) >> 6;
            Csd->PermWrProtect = (CSD_Tab[14] & 0x20) >> 5;
            Csd->TempWrProtect = (CSD_Tab[14] & 0x10) >> 4;
            Csd->FileFormat = (CSD_Tab[14] & 0x0C) >> 2;
            Csd->Reserved4 = (CSD_Tab[14] & 0x03);
            Csd->crc = (CSD_Tab[15] & 0xFE) >> 1;
            Csd->Reserved5 = (CSD_Tab[15] & 0x01);

            retr = SD_SUCCESS;
        }
    }

    /* Send dummy byte: 8 Clock pulses of delay */
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);

    /* Return the reponse */
    return retr;
}

STATIC sd_result_t SD_GetCIDRegister(SD_CID *Cid) {
    uint32_t counter = 0;
    sd_result_t retr = SD_FAILED;
    uint8_t CID_Tab[16];
    SD_CmdAnswer_typedef response;

    /* Send CMD10 (CID register) and Wait for response in the R1 format (0x00 is no errors) */
    SD_SendCmd(SD_CMD_SEND_CID, 0, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
    if (response.r1 == SD_R1_NO_ERROR) {
        if (SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == SD_SUCCESS) {
            /* Store CID register value on CID_Tab */
            for (counter = 0; counter < 16; counter++)
            {
                CID_Tab[counter] = SD_IO_WriteByte(SD_DUMMY_BYTE);
            }

            /* Get CRC bytes (not really needed by us, but required by SD) */
            SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /* Byte 0 */
            Cid->ManufacturerID = CID_Tab[0];

            /* Byte 1 */
            Cid->OEM_AppliID = CID_Tab[1] << 8;

            /* Byte 2 */
            Cid->OEM_AppliID |= CID_Tab[2];

            /* Byte 3 */
            Cid->ProdName1 = CID_Tab[3] << 24;

            /* Byte 4 */
            Cid->ProdName1 |= CID_Tab[4] << 16;

            /* Byte 5 */
            Cid->ProdName1 |= CID_Tab[5] << 8;

            /* Byte 6 */
            Cid->ProdName1 |= CID_Tab[6];

            /* Byte 7 */
            Cid->ProdName2 = CID_Tab[7];

            /* Byte 8 */
            Cid->ProdRev = CID_Tab[8];

            /* Byte 9 */
            Cid->ProdSN = CID_Tab[9] << 24;

            /* Byte 10 */
            Cid->ProdSN |= CID_Tab[10] << 16;

            /* Byte 11 */
            Cid->ProdSN |= CID_Tab[11] << 8;

            /* Byte 12 */
            Cid->ProdSN |= CID_Tab[12];

            /* Byte 13 */
            Cid->Reserved1 |= (CID_Tab[13] & 0xF0) >> 4;
            Cid->ManufactDate = (CID_Tab[13] & 0x0F) << 8;

            /* Byte 14 */
            Cid->ManufactDate |= CID_Tab[14];

            /* Byte 15 */
            Cid->CID_CRC = (CID_Tab[15] & 0xFE) >> 1;
            Cid->Reserved2 = 1;

            retr = SD_SUCCESS;
        }
    }

    /* Send dummy byte: 8 Clock pulses of delay */
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);

    /* Return the reponse */
    return retr;
}




// ------------------------------------------------------------------------------------------------
// interrupt handlers
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// public functions
// ------------------------------------------------------------------------------------------------

void sdcard_init(void) {
    #if defined(MICROPY_HW_SDCARD_DETECT_PIN)
    mp_hal_pin_config(MICROPY_HW_SDCARD_DETECT_PIN, MP_HAL_PIN_MODE_INPUT, MICROPY_HW_SDCARD_DETECT_PULL, 0);
    #endif
}
bool sdcard_is_present(void) {
    return HAL_GPIO_ReadPin(MICROPY_HW_SDCARD_DETECT_PIN->gpio, MICROPY_HW_SDCARD_DETECT_PIN->pin_mask) == MICROPY_HW_SDCARD_DETECT_PRESENT;
}
bool sdcard_power_on(void) {
    if (sdcard_is_present()) {
        SD_IO_Init();
        SD_GoIdleState();
        return true;
    }
    return false;
}
void sdcard_power_off(void) {

}
uint64_t sdcard_get_capacity_in_bytes(void) {
    uint8_t status;

    status = SD_GetCSDRegister(&(sd_handle.Csd));
    status |= SD_GetCIDRegister(&(sd_handle.Cid));

    if (flag_SDHC == 1) {
        sd_handle.LogBlockSize = SD_BLOCK_SIZE;
        sd_handle.CardBlockSize = SD_BLOCK_SIZE;
        sd_handle.CardCapacity = (sd_handle.Csd.version.v2.DeviceSize + 1) * 1024 * sd_handle.LogBlockSize;
        sd_handle.LogBlockNbr = (sd_handle.CardCapacity) / (sd_handle.LogBlockSize);
    } else {
        sd_handle.CardCapacity = (sd_handle.Csd.version.v1.DeviceSize + 1);
        sd_handle.CardCapacity *= (1 << (sd_handle.Csd.version.v1.DeviceSizeMul + 2));
        sd_handle.LogBlockSize = SD_BLOCK_SIZE;
        sd_handle.CardBlockSize = 1 << (sd_handle.Csd.RdBlockLen);
        sd_handle.CardCapacity *= sd_handle.CardBlockSize;
        sd_handle.LogBlockNbr = (sd_handle.CardCapacity) / (sd_handle.LogBlockSize);
    }

    return sd_handle.CardCapacity;
}

STATIC HAL_StatusTypeDef sdcard_get_status(void) {
    SD_CmdAnswer_typedef retr;

    /* Send CMD13 (SD_SEND_STATUS) to get SD status */
    SD_SendCmd(SD_CMD_SEND_STATUS, 0, 0xFF, SD_ANSWER_R2_EXPECTED, &retr);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);

    /* Find SD status according to card state */
    if ((retr.r1 == SD_R1_NO_ERROR) && (retr.r2 == SD_R2_NO_ERROR)) {
        return HAL_OK;
    }

    return HAL_ERROR;
}

STATIC HAL_StatusTypeDef sdcard_wait_finished(uint32_t timeout) {

    uint32_t start = HAL_GetTick();
    // Wait for SD card to complete the operation
    for (;;) {
        uint32_t state;
        {
            state = sdcard_get_status();
        }
        if (state == HAL_OK) {
            return HAL_OK;
        } else if (HAL_GetTick() - start >= timeout) {
            return HAL_TIMEOUT;
        }
        __WFI();
    }
    return HAL_OK;
}

mp_uint_t sdcard_read_blocks(uint8_t *dest, uint32_t block_num, uint32_t num_blocks) {
    sd_result_t sd_result = SD_SUCCESS;

    uint32_t offset = 0;
    uint32_t address_to_read = 0;

    uint32_t nb_block_to_read = num_blocks;

    uint8_t *ptr = NULL;
    SD_CmdAnswer_typedef response;

    /* Send CMD16 (SD_CMD_SET_BLOCKLEN) to set the size of the block and
    Check if the SD acknowledged the set block length command: R1 response (0x00: no errors) */
    SD_SendCmd(SD_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if (response.r1 != SD_R1_NO_ERROR) {
        sd_result = SD_READ_ERROR;
        printf(" Unexpected error while reading\n");
    } else {
        ptr = sd_buffer_rw;

        memset(ptr, SD_DUMMY_BYTE, sizeof(uint8_t) * SD_BLOCK_SIZE);

        /* Initialize the address */
        // address_to_read = (block_num * ((flag_SDHC == 1) ? 1 : SD_BLOCK_SIZE));
        address_to_read = block_num;
        /* Data transfer */
        while (nb_block_to_read--) {
            /* Send CMD17 (SD_CMD_READ_SINGLE_BLOCK) to read one block */
            /* Check if the SD acknowledged the read block command: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_READ_SINGLE_BLOCK, address_to_read, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            if (response.r1 != SD_R1_NO_ERROR) {
                sd_result = SD_READ_ERROR;
                printf(" Unexpected error while reading : n° %lu block \n", nb_block_to_read);
                break;
            }

            /* Now look for the data token to signify the start of the data */
            if (SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == SD_SUCCESS) {
                /* Read the SD block data : read NumByteToRead data */
                SD_IO_WriteReadData(ptr, (uint8_t *)dest + offset, SD_BLOCK_SIZE);

                /* Set next read address*/
                offset += SD_BLOCK_SIZE;
                // address_to_read = ((flag_SDHC == 1) ? (address_to_read + 1) : (address_to_read + SD_BLOCK_SIZE));
                address_to_read++;
                /* get CRC bytes (not really needed by us, but required by SD) */
                SD_IO_WriteByte(SD_DUMMY_BYTE);
                SD_IO_WriteByte(SD_DUMMY_BYTE);
            } else {
                sd_result = SD_READ_ERROR;
                printf(" Unexpected error while reading : n° %lu block \n", nb_block_to_read);
                break;
            }

            /* End the command data read cycle */
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        }
    }


    if (sd_result == SD_READ_ERROR) {
        /* Send dummy byte: 8 Clock pulses of delay */
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
    }

    sdcard_wait_finished(60000);

    /* Return the reponse */
    return sd_result;
}
mp_uint_t sdcard_write_blocks(const uint8_t *src, uint32_t block_num, uint32_t num_blocks) {
    sd_result_t sd_result = SD_SUCCESS;

    uint32_t offset = 0;
    uint32_t address_to_write = 0;
    uint8_t *ptr = NULL;
    uint32_t nb_block_to_write = num_blocks;

    SD_CmdAnswer_typedef response;

    /* Send CMD16 (SD_CMD_SET_BLOCKLEN) to set the size of the block and
    Check if the SD acknowledged the set block length command: R1 response (0x00: no errors) */
    SD_SendCmd(SD_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);

    if (response.r1 != SD_R1_NO_ERROR) {
        sd_result = SD_WRITE_ERROR;
        printf(" Unexpected error while writing\n");
    } else {

        ptr = sd_buffer_rw;

        /* Initialize the address */
        // address_to_write = (block_num * ((flag_SDHC == 1) ? 1 : SD_BLOCK_SIZE));
        address_to_write = block_num;
        /* Data transfer */
        while (nb_block_to_write--) {
            /* Send CMD24 (SD_CMD_WRITE_SINGLE_BLOCK) to write blocks  and
        Check if the SD acknowledged the write block command: R1 response (0x00: no errors) */
            SD_SendCmd(SD_CMD_WRITE_SINGLE_BLOCK, address_to_write, 0xFF, SD_ANSWER_R1_EXPECTED, &response);
            if (response.r1 != SD_R1_NO_ERROR) {
                sd_result = SD_WRITE_ERROR;
                printf(" Unexpected error while writing \n");
                break;
            }

            /* Send dummy byte for NWR timing : one byte between CMDWRITE and TOKEN */
            SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /* Send the data token to signify the start of the data */
            SD_IO_WriteByte(SD_TOKEN_START_DATA_SINGLE_BLOCK_WRITE);

            /* Write the block data to SD */
            SD_IO_WriteReadData((uint8_t *)src + offset, ptr, SD_BLOCK_SIZE);

            /* Set next write address */
            offset += SD_BLOCK_SIZE;
            // address_to_write = ((flag_SDHC == 1) ? (address_to_write + 1) : (address_to_write + SD_BLOCK_SIZE));
            address_to_write++;
            /* Put CRC bytes (not really needed by us, but required by SD) */
            SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE);

            /* Read data response */
            if (SD_GetDataResponse() != SD_DATA_OK) {
                /* Set response value to failure */
                sd_result = SD_WRITE_ERROR;
                printf(" Unexpected error while writing, n° %lu block \n", nb_block_to_write);
                break;
            }

            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        }
    }

    if (sd_result == SD_WRITE_ERROR) {
        /* Send dummy byte: 8 Clock pulses of delay */
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
    }

    sdcard_wait_finished(60000);

    return sd_result;

}

STATIC mp_obj_t pyb_sdcard_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return singleton object
    return MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
}

STATIC mp_obj_t sd_present(mp_obj_t self) {
    return mp_obj_new_bool(sdcard_is_present());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_present_obj, sd_present);

STATIC mp_obj_t sd_power(mp_obj_t self, mp_obj_t state) {
    bool result;
    if (mp_obj_is_true(state)) {
        result = sdcard_power_on();
    } else {
        sdcard_power_off();
        result = true;
    }
    return mp_obj_new_bool(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_power_obj, sd_power);

STATIC mp_obj_t sd_info(mp_obj_t self) {
    uint32_t card_type;
    uint32_t log_block_nbr;
    uint32_t log_block_size;
    {
        card_type = flag_SDHC;
        log_block_nbr = sd_handle.LogBlockNbr;
        log_block_size = sd_handle.LogBlockSize;
    }
    // cardinfo.SD_csd and cardinfo.SD_cid have lots of info but we don't use them
    mp_obj_t tuple[3] = {
        mp_obj_new_int_from_ull((uint64_t)log_block_nbr * (uint64_t)log_block_size),
        mp_obj_new_int_from_uint(log_block_size),
        mp_obj_new_int(card_type),
    };
    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_info_obj, sd_info);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_read(mp_obj_t self, mp_obj_t block_num) {
    uint8_t *dest = m_new(uint8_t, SDCARD_BLOCK_SIZE);
    mp_uint_t ret = sdcard_read_blocks(dest, mp_obj_get_int(block_num), 1);

    if (ret != 0) {
        m_del(uint8_t, dest, SDCARD_BLOCK_SIZE);
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("sdcard_read_blocks failed [%u]"), ret);
    }

    return mp_obj_new_bytearray_by_ref(SDCARD_BLOCK_SIZE, dest);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_read_obj, sd_read);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_write(mp_obj_t self, mp_obj_t block_num, mp_obj_t data) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len % SDCARD_BLOCK_SIZE != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("writes must be a multiple of %d bytes"), SDCARD_BLOCK_SIZE);
    }

    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);

    if (ret != 0) {
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("sdcard_write_blocks failed [%u]"), ret);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(sd_write_obj, sd_write);

STATIC mp_obj_t pyb_sdcard_readblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    mp_uint_t ret = sdcard_read_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_readblocks_obj, pyb_sdcard_readblocks);

STATIC mp_obj_t pyb_sdcard_writeblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_writeblocks_obj, pyb_sdcard_writeblocks);

STATIC mp_obj_t pyb_sdcard_ioctl(mp_obj_t self, mp_obj_t cmd_in, mp_obj_t arg_in) {
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT:
            if (!sdcard_power_on()) {
                return MP_OBJ_NEW_SMALL_INT(-1); // error
            }
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_DEINIT:
            sdcard_power_off();
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_SYNC:
            // nothing to do
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(sdcard_get_capacity_in_bytes() / SDCARD_BLOCK_SIZE);

        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(SDCARD_BLOCK_SIZE);

        default: // unknown command
            return MP_OBJ_NEW_SMALL_INT(-1); // error
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_ioctl_obj, pyb_sdcard_ioctl);

STATIC const mp_rom_map_elem_t pyb_sdcard_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&sd_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_power), MP_ROM_PTR(&sd_power_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&sd_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&sd_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&sd_write_obj) },
    // block device protocol
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&pyb_sdcard_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&pyb_sdcard_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&pyb_sdcard_ioctl_obj) },
};

STATIC MP_DEFINE_CONST_DICT(pyb_sdcard_locals_dict, pyb_sdcard_locals_dict_table);

const mp_obj_type_t pyb_sdcard_type = {
    { &mp_type_type },
    .name = MP_QSTR_SDCard,
    .make_new = pyb_sdcard_make_new,
    .locals_dict = (mp_obj_dict_t *)&pyb_sdcard_locals_dict,
};

void sdcard_init_vfs(fs_user_mount_t *vfs, int part) {
    vfs->base.type = &mp_fat_vfs_type;
    vfs->blockdev.flags |= MP_BLOCKDEV_FLAG_NATIVE | MP_BLOCKDEV_FLAG_HAVE_IOCTL;
    vfs->fatfs.drv = vfs;
    vfs->fatfs.part = part;
    vfs->blockdev.readblocks[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_readblocks_obj);
    vfs->blockdev.readblocks[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
    vfs->blockdev.readblocks[2] = MP_OBJ_FROM_PTR(sdcard_read_blocks); // native version
    vfs->blockdev.writeblocks[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_writeblocks_obj);
    vfs->blockdev.writeblocks[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
    vfs->blockdev.writeblocks[2] = MP_OBJ_FROM_PTR(sdcard_write_blocks); // native version
    vfs->blockdev.u.ioctl[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_ioctl_obj);
    vfs->blockdev.u.ioctl[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
}

#endif  // STM32WB

#endif // MICROPY_HW_ENABLE_SDCARD || MICROPY_HW_ENABLE_MMCARD
