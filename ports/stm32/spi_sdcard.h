#ifndef SPI_SDCARD_H
#define SPI_SDCARD_H


// ------------------------------------------------------------------------------------------------
// includes
// ------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// constant macros
// ------------------------------------------------------------------------------------------------

#define SDCARD_BLOCK_SIZE (512)

// ------------------------------------------------------------------------------------------------
// function macros
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// typedefs, structures, unions and enums
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// public variables
// ------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// function prototypes
// ------------------------------------------------------------------------------------------------


void sdcard_init(void);
bool sdcard_is_present(void);
bool sdcard_power_on(void);
void sdcard_power_off(void);
uint64_t sdcard_get_capacity_in_bytes(void);

mp_uint_t sdcard_read_blocks(uint8_t *dest, uint32_t block_num, uint32_t num_blocks);
mp_uint_t sdcard_write_blocks(const uint8_t *src, uint32_t block_num, uint32_t num_blocks);

extern const struct _mp_obj_type_t pyb_sdcard_type;
extern const struct _mp_obj_type_t pyb_mmcard_type;
extern const struct _mp_obj_base_t pyb_sdcard_obj;

struct _fs_user_mount_t;
void sdcard_init_vfs(struct _fs_user_mount_t *vfs, int part);

#endif // SPI_SDCARD_H