// ------------------------------------------------------------------------------------------------
// includes
// ------------------------------------------------------------------------------------------------
#include <stdio.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "sai.h"
#include "timer.h"

#include "cca02m2_conf.h"
#include "cca02m2_audio.h"

#include "extmod/vfs_fat.h"
#include "storage.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "lib/oofatfs/ff.h"


// ------------------------------------------------------------------------------------------------
// private constant macros
// ------------------------------------------------------------------------------------------------

#define PDM_BUFFER_SIZE (((((AUDIO_IN_CHANNELS * AUDIO_IN_SAMPLING_FREQUENCY) / 1000) * MAX_DECIMATION_FACTOR) / 16)* N_MS_PER_INTERRUPT )
#define PCM_BUFFER_SIZE (((AUDIO_IN_CHANNELS*AUDIO_IN_SAMPLING_FREQUENCY)/1000)  * N_MS_PER_INTERRUPT)

// ------------------------------------------------------------------------------------------------
// private function macros
// ------------------------------------------------------------------------------------------------

#define RIFF_HEADER     ({ 'R','I','F','F'})
#define WAVE_HEADER     ({ "W","A","V","E"})
#define FMT_HEADER      ({ "F","M","T",""})
#define DATA_HEADER     ({ "d","a","t","a"})

// ------------------------------------------------------------------------------------------------
// private typedefs, structures, unions and enums
// ------------------------------------------------------------------------------------------------

#pragma pack(8)
typedef struct wav_header_t
{
    uint8_t     FileTypeBlocID[4];
    uint32_t    FileSize;
    char        FileFormatID[4];
    uint8_t     FormatBlocID[4];
    uint32_t    BlocSize;
    uint16_t    AudioFormat;
    uint16_t    NbrCanaux;
    uint32_t    Frequence;
    uint32_t    BytePerSec;
    uint16_t    BytePerBloc;
    uint16_t    BitsPerSample;
    uint8_t     DataBlocID[4];
    uint32_t    DataSize;
}wav_header_t;
#pragma pack(1)

typedef struct pyb_sai_obj_t 
{
    mp_obj_base_t base;
    uint16_t PDM_Buffer[PDM_BUFFER_SIZE];
    uint16_t PCM_Buffer[PCM_BUFFER_SIZE];
    CCA02M2_AUDIO_Init_t MicParams;
} pyb_sai_obj_t;

// ------------------------------------------------------------------------------------------------
// private variables
// ------------------------------------------------------------------------------------------------

STATIC pyb_sai_obj_t pyb_sai_obj;

volatile bool new_data = false;

// ------------------------------------------------------------------------------------------------
// public variables
// ------------------------------------------------------------------------------------------------

extern SAI_HandleTypeDef            hAudioInSai;

// ------------------------------------------------------------------------------------------------
// private function prototypes
// ------------------------------------------------------------------------------------------------
static void audio_process(void);

// ------------------------------------------------------------------------------------------------
// private functions
// ------------------------------------------------------------------------------------------------

static void audio_process(void)
{
    CCA02M2_AUDIO_IN_PDMToPCM(CCA02M2_AUDIO_INSTANCE,(uint16_t * )pyb_sai_obj.PDM_Buffer, pyb_sai_obj.PCM_Buffer);    


    new_data = true;
}

// ------------------------------------------------------------------------------------------------
// interrupt handlers
// ------------------------------------------------------------------------------------------------
void DMA1_Channel5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(hAudioInSai.hdmarx);
}

void CCA02M2_AUDIO_IN_HalfTransfer_CallBack(uint32_t Instance)
{  
    audio_process();
}

void CCA02M2_AUDIO_IN_TransferComplete_CallBack(uint32_t Instance)
{  
    audio_process();
}
// ------------------------------------------------------------------------------------------------
// public functions
// ------------------------------------------------------------------------------------------------

void sai_init(void) 
{
    pyb_sai_obj.MicParams.BitsPerSample = 16;
    pyb_sai_obj.MicParams.ChannelsNbr = AUDIO_IN_CHANNELS;
    pyb_sai_obj.MicParams.Device = AUDIO_IN_DIGITAL_MIC;
    pyb_sai_obj.MicParams.SampleRate = AUDIO_IN_SAMPLING_FREQUENCY;
    pyb_sai_obj.MicParams.Volume = AUDIO_VOLUME_INPUT;

    CCA02M2_AUDIO_IN_Init(CCA02M2_AUDIO_INSTANCE, &pyb_sai_obj.MicParams);
}


/// \classmethod \constructor(id)
/// Create a sai object.  `id` is 1-4.
STATIC mp_obj_t pyb_sai_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) 
{
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    pyb_sai_obj_t *sai = m_new_obj(pyb_sai_obj_t);
    sai->base.type = &pyb_sai_type;

    sai_init();

    return MP_OBJ_FROM_PTR(sai);
}

STATIC mp_obj_t pyb_sai_record(mp_obj_t self_in, mp_obj_t file_name, mp_obj_t time_in_s) 
{
    // pyb_sai_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const char *p_in;
    const char *p_out;


    uint32_t fille_size = 0;
    uint32_t nb_write = 0;
    FIL fp;
    UINT n;

    p_in = mp_obj_str_get_str(file_name);
    uint32_t recording_time_ms = mp_obj_get_int(time_in_s) * 1000;

    mp_vfs_mount_t * mp_vfs = mp_vfs_lookup_path(p_in, &p_out);

    fs_user_mount_t * vfs = (fs_user_mount_t *) mp_vfs->obj;

    FATFS fatfs = vfs->fatfs;

    FRESULT res = f_open(&fatfs, &fp, p_in, FA_WRITE | FA_CREATE_ALWAYS);

    if (res != FR_OK) 
    {
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("Unable to open File :%d"), res);
        return mp_const_none;
    }

    wav_header_t wav_header = 
    { 
        .FileTypeBlocID = { 'R','I','F','F'},
        .FileSize = 0,
        .FileFormatID = { 'W','A','V','E'},
        .FormatBlocID = { 'f','m','t', 0x20},
        .BlocSize = 16,
        .AudioFormat = 1, // PCM
        .NbrCanaux = AUDIO_IN_CHANNELS,
        .Frequence = AUDIO_IN_SAMPLING_FREQUENCY,
        .BytePerSec = AUDIO_IN_SAMPLING_FREQUENCY * 16 / 8 * AUDIO_IN_CHANNELS,
        .BytePerBloc = 2, // BlockSize * AUDIO_IN_CHANNELS / 8
        .BitsPerSample = 16,
        .DataBlocID = { 'd','a','t','a'},
        .DataSize = 0,
    };

    res = f_write(&fp, (uint8_t *) &wav_header, sizeof(wav_header_t), &n);

    if (res != FR_OK) 
    {
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("Unable to write File :%d"), res);
        return mp_const_none;
    }

    f_sync(&fp);

    CCA02M2_AUDIO_IN_Record(CCA02M2_AUDIO_INSTANCE, (uint8_t *) pyb_sai_obj.PDM_Buffer, AUDIO_IN_BUFFER_SIZE);

    new_data = false;

    while(nb_write < (recording_time_ms / N_MS_PER_INTERRUPT))
    {       
        if(new_data == true)
        {
            new_data = false;

            f_write(&fp, (uint8_t *)pyb_sai_obj.PCM_Buffer, PCM_BUFFER_SIZE * sizeof(uint16_t), &n);

            nb_write++;
        }
        
        __WFI();
    }

    CCA02M2_AUDIO_IN_Stop(CCA02M2_AUDIO_INSTANCE);


    wav_header.FileSize = fille_size + sizeof(wav_header_t) - 8;
    wav_header.DataSize = fille_size;

    res = f_lseek(&fp, 0);

    if (res != FR_OK) 
    {
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("Unable to fseek File :%d"), res);
        return mp_const_none;
    }
    

    res = f_write(&fp, (uint8_t *) &wav_header, sizeof(wav_header_t), &n);

    if (res != FR_OK) 
    {
        mp_raise_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("Unable to write File :%d"), res);
        return mp_const_none;
    }
    f_sync(&fp);
    
    f_close(&fp);

    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sai_record_obj, pyb_sai_record);

STATIC const mp_rom_map_elem_t pyb_sai_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_record), MP_ROM_PTR(&pyb_sai_record_obj) },
};

STATIC MP_DEFINE_CONST_DICT(pyb_sai_locals_dict, pyb_sai_locals_dict_table);

const mp_obj_type_t pyb_sai_type = {
    { &mp_type_type },
    .name = MP_QSTR_Sai,
    .make_new = pyb_sai_make_new,
    .locals_dict = (mp_obj_dict_t *)&pyb_sai_locals_dict,
};