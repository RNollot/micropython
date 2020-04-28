#ifndef SD_SPI_HAL
#define SD_SPI_HAL

#ifdef __cplusplus
 extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// includes
// ------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// constant macros
// ------------------------------------------------------------------------------------------------

#define SD_DUMMY_BYTE            				 0xFF
#define SD_MAX_FRAME_LENGTH        				 17    /* Lenght = 16 + 1 */
#define SD_CMD_LENGTH               			 6
#define SD_MAX_TRY               				 100    /* Number of try */
#define SD_CSD_STRUCT_V1         				 0x2    /* CSD struct version V1 */
#define SD_CSD_STRUCT_V2         				 0x1    /* CSD struct version V2 */

#define SD_TOKEN_START_DATA_SINGLE_BLOCK_READ    0xFE  /* Data token start byte, Start Single Block Read */
#define SD_TOKEN_START_DATA_MULTIPLE_BLOCK_READ  0xFE  /* Data token start byte, Start Multiple Block Read */
#define SD_TOKEN_START_DATA_SINGLE_BLOCK_WRITE   0xFE  /* Data token start byte, Start Single Block Write */
#define SD_TOKEN_START_DATA_MULTIPLE_BLOCK_WRITE 0xFD  /* Data token start byte, Start Multiple Block Write */
#define SD_TOKEN_STOP_DATA_MULTIPLE_BLOCK_WRITE  0xFD  /* Data toke stop byte, Stop Multiple Block Write */

#define SD_CMD_GO_IDLE_STATE          			 0   /* CMD0 = 0x40  */
#define SD_CMD_SEND_OP_COND           			 1   /* CMD1 = 0x41  */
#define SD_CMD_SEND_IF_COND           			 8   /* CMD8 = 0x48  */
#define SD_CMD_SEND_CSD               			 9   /* CMD9 = 0x49  */
#define SD_CMD_SEND_CID               			 10  /* CMD10 = 0x4A */
#define SD_CMD_STOP_TRANSMISSION      			 12  /* CMD12 = 0x4C */
#define SD_CMD_SEND_STATUS            			 13  /* CMD13 = 0x4D */
#define SD_CMD_SET_BLOCKLEN           			 16  /* CMD16 = 0x50 */
#define SD_CMD_READ_SINGLE_BLOCK      			 17  /* CMD17 = 0x51 */
#define SD_CMD_READ_MULT_BLOCK        			 18  /* CMD18 = 0x52 */
#define SD_CMD_SET_BLOCK_COUNT        			 23  /* CMD23 = 0x57 */
#define SD_CMD_WRITE_SINGLE_BLOCK     			 24  /* CMD24 = 0x58 */
#define SD_CMD_WRITE_MULT_BLOCK       			 25  /* CMD25 = 0x59 */
#define SD_CMD_PROG_CSD               			 27  /* CMD27 = 0x5B */
#define SD_CMD_SET_WRITE_PROT         			 28  /* CMD28 = 0x5C */
#define SD_CMD_CLR_WRITE_PROT         			 29  /* CMD29 = 0x5D */
#define SD_CMD_SEND_WRITE_PROT        			 30  /* CMD30 = 0x5E */
#define SD_CMD_SD_ERASE_GRP_START     			 32  /* CMD32 = 0x60 */
#define SD_CMD_SD_ERASE_GRP_END       			 33  /* CMD33 = 0x61 */
#define SD_CMD_UNTAG_SECTOR           			 34  /* CMD34 = 0x62 */
#define SD_CMD_ERASE_GRP_START        			 35  /* CMD35 = 0x63 */
#define SD_CMD_ERASE_GRP_END          			 36  /* CMD36 = 0x64 */
#define SD_CMD_UNTAG_ERASE_GROUP      			 37  /* CMD37 = 0x65 */
#define SD_CMD_ERASE                  			 38  /* CMD38 = 0x66 */
#define SD_CMD_SD_APP_OP_COND         			 41  /* CMD41 = 0x69 */
#define SD_CMD_APP_CMD                			 55  /* CMD55 = 0x77 */
#define SD_CMD_READ_OCR               			 58  /* CMD55 = 0x79 */


// ------------------------------------------------------------------------------------------------
// function macros
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// typedefs, structures, unions and enums
// ------------------------------------------------------------------------------------------------


typedef struct              
{
  uint8_t  Reserved1:2;               /* Reserved */
  uint16_t DeviceSize:12;             /* Device Size */
  uint8_t  MaxRdCurrentVDDMin:3;      /* Max. read current @ VDD min */
  uint8_t  MaxRdCurrentVDDMax:3;      /* Max. read current @ VDD max */
  uint8_t  MaxWrCurrentVDDMin:3;      /* Max. write current @ VDD min */
  uint8_t  MaxWrCurrentVDDMax:3;      /* Max. write current @ VDD max */
  uint8_t  DeviceSizeMul:3;           /* Device size multiplier */
} struct_v1;


typedef struct              
{
  uint8_t  Reserved1:6;               /* Reserved */
  uint32_t DeviceSize:22;             /* Device Size */
  uint8_t  Reserved2:1;               /* Reserved */
} struct_v2;

typedef struct
{
  /* Header part */
  uint8_t  CSDStruct:2;            /* CSD structure */
  uint8_t  Reserved1:6;            /* Reserved */
  uint8_t  TAAC:8;                 /* Data read access-time 1 */
  uint8_t  NSAC:8;                 /* Data read access-time 2 in CLK cycles */
  uint8_t  MaxBusClkFrec:8;        /* Max. bus clock frequency */
  uint16_t CardComdClasses:12;      /* Card command classes */
  uint8_t  RdBlockLen:4;           /* Max. read data block length */
  uint8_t  PartBlockRead:1;        /* Partial blocks for read allowed */
  uint8_t  WrBlockMisalign:1;      /* Write block misalignment */
  uint8_t  RdBlockMisalign:1;      /* Read block misalignment */
  uint8_t  DSRImpl:1;              /* DSR implemented */
  
  /* v1 or v2 struct */
  union csd_version {
    struct_v1 v1;
    struct_v2 v2;
  } version;
  
  uint8_t  EraseSingleBlockEnable:1;  /* Erase single block enable */
  uint8_t  EraseSectorSize:7;         /* Erase group size multiplier */
  uint8_t  WrProtectGrSize:7;         /* Write protect group size */
  uint8_t  WrProtectGrEnable:1;       /* Write protect group enable */
  uint8_t  Reserved2:2;               /* Reserved */
  uint8_t  WrSpeedFact:3;             /* Write speed factor */
  uint8_t  MaxWrBlockLen:4;           /* Max. write data block length */
  uint8_t  WriteBlockPartial:1;       /* Partial blocks for write allowed */
  uint8_t  Reserved3:5;               /* Reserved */
  uint8_t  FileFormatGrouop:1;        /* File format group */
  uint8_t  CopyFlag:1;                /* Copy flag (OTP) */
  uint8_t  PermWrProtect:1;           /* Permanent write protection */
  uint8_t  TempWrProtect:1;           /* Temporary write protection */
  uint8_t  FileFormat:2;              /* File Format */
  uint8_t  Reserved4:2;               /* Reserved */
  uint8_t  crc:7;                     /* Reserved */
  uint8_t  Reserved5:1;               /* always 1*/
  
} SD_CSD;

typedef struct
{
  __IO uint8_t  ManufacturerID;       /* ManufacturerID */
  __IO uint16_t OEM_AppliID;          /* OEM/Application ID */
  __IO uint32_t ProdName1;            /* Product Name part1 */
  __IO uint8_t  ProdName2;            /* Product Name part2*/
  __IO uint8_t  ProdRev;              /* Product Revision */
  __IO uint32_t ProdSN;               /* Product Serial Number */
  __IO uint8_t  Reserved1;            /* Reserved1 */
  __IO uint16_t ManufactDate;         /* Manufacturing Date */
  __IO uint8_t  CID_CRC;              /* CID CRC */
  __IO uint8_t  Reserved2;            /* always 1 */
} SD_CID;

typedef struct
{
  SD_CSD Csd;
  SD_CID Cid;
  uint32_t CardCapacity;              /*!< Card Capacity */
  uint32_t CardBlockSize;             /*!< Card Block Size */
  uint32_t LogBlockNbr;               /*!< Specifies the Card logical Capacity in blocks   */
  uint32_t LogBlockSize;              /*!< Specifies logical block size in bytes           */
} SD_CardInfo;


typedef struct 
{
	uint8_t r1;
	uint8_t r2;
	uint8_t r3;
	uint8_t r4;
	uint8_t r5;
} SD_CmdAnswer_typedef;

typedef enum 
{
	SD_ANSWER_R1_EXPECTED,
	SD_ANSWER_R1B_EXPECTED,
	SD_ANSWER_R2_EXPECTED,
	SD_ANSWER_R3_EXPECTED,
	SD_ANSWER_R4R5_EXPECTED,
	SD_ANSWER_R7_EXPECTED,
}SD_Answer_type;

typedef enum
{
/* R1 answer value */
  SD_R1_NO_ERROR            = (0x00),
  SD_R1_IN_IDLE_STATE       = (0x01),
  SD_R1_ERASE_RESET         = (0x02),
  SD_R1_ILLEGAL_COMMAND     = (0x04),
  SD_R1_COM_CRC_ERROR       = (0x08),
  SD_R1_ERASE_SEQUENCE_ERROR= (0x10),
  SD_R1_ADDRESS_ERROR       = (0x20),
  SD_R1_PARAMETER_ERROR     = (0x40),

/* R2 answer value */
  SD_R2_NO_ERROR            = 0x00,
  SD_R2_CARD_LOCKED         = 0x01,
  SD_R2_LOCKUNLOCK_ERROR    = 0x02,
  SD_R2_ERROR               = 0x04,
  SD_R2_CC_ERROR            = 0x08,
  SD_R2_CARD_ECC_FAILED     = 0x10,
  SD_R2_WP_VIOLATION        = 0x20,
  SD_R2_ERASE_PARAM         = 0x40,
  SD_R2_OUTOFRANGE          = 0x80,

/**
  * @brief  Data response error
  */
  SD_DATA_OK                = (0x05),
  SD_DATA_CRC_ERROR         = (0x0B),
  SD_DATA_WRITE_ERROR       = (0x0D),
  SD_DATA_OTHER_ERROR       = (0xFF)
} SD_Error;

// ------------------------------------------------------------------------------------------------
// public variables
// ------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// function prototypes
// ------------------------------------------------------------------------------------------------


#ifdef __cplusplus
}
#endif

#endif // SD_SPI_HAL
