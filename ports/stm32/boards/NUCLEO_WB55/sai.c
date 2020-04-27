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

#include "ifl_deque.h"
// ------------------------------------------------------------------------------------------------
// private constant macros
// ------------------------------------------------------------------------------------------------

#define PDM_BUFFER_SIZE (((((AUDIO_IN_CHANNELS * AUDIO_IN_SAMPLING_FREQUENCY) / 1000) * MAX_DECIMATION_FACTOR) / 16)* N_MS_PER_INTERRUPT )
#define PCM_BUFFER_SIZE (((AUDIO_IN_CHANNELS*AUDIO_IN_SAMPLING_FREQUENCY)/1000)  * N_MS_PER_INTERRUPT)

#define RECORDING_TIME      (1000)
#define QUEUE_SIZE          (50)
// ------------------------------------------------------------------------------------------------
// private function macros
// ------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
// private typedefs, structures, unions and enums
// ------------------------------------------------------------------------------------------------
typedef struct pcm_buffer_t 
{
    uint16_t PCM_Buffer[PCM_BUFFER_SIZE];
}pcm_buffer_t;


typedef struct pyb_sai_obj_t 
{
    mp_obj_base_t base;
    uint16_t PDM_Buffer[PDM_BUFFER_SIZE];
    pcm_buffer_t pcm_buffer;
    CCA02M2_AUDIO_Init_t MicParams;
    IFL_DEQUE_DECLARE(audio_queue, pcm_buffer_t, QUEUE_SIZE);
} pyb_sai_obj_t;

// ------------------------------------------------------------------------------------------------
// private variables
// ------------------------------------------------------------------------------------------------

const char * filename = "audio_test.wav";
STATIC pyb_sai_obj_t pyb_sai_obj;

STATIC uint32_t nb_write = 0;
STATIC uint32_t ts = 0;

volatile bool end_record = false;

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
    CCA02M2_AUDIO_IN_PDMToPCM(CCA02M2_AUDIO_INSTANCE,(uint16_t * )pyb_sai_obj.PDM_Buffer, pyb_sai_obj.pcm_buffer.PCM_Buffer);    
    
    __disable_irq();
    IFL_DEQUE_PUSH_BACK(&pyb_sai_obj.audio_queue, &pyb_sai_obj.pcm_buffer);
    __enable_irq();

    nb_write++;

    if(nb_write > (1000 / N_MS_PER_INTERRUPT))
    {
        printf("End TS : %ld  %ld\n",  HAL_GetTick(),  HAL_GetTick() - ts);
        
        CCA02M2_AUDIO_IN_Stop(CCA02M2_AUDIO_INSTANCE);

        end_record = true;
    }

}

// ------------------------------------------------------------------------------------------------
// interrupt handlers
// ------------------------------------------------------------------------------------------------
void DMA1_Channel1_IRQHandler(void)
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

    IFL_DEQUE_INIT(&pyb_sai_obj.audio_queue);
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

STATIC mp_obj_t pyb_sai_record(mp_obj_t self_in, mp_obj_t file_name, mp_obj_t time_in) 
{
    // pyb_sai_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const char *p_in;
    const char *p_out;

    nb_write = 0;
    end_record = false;

    p_in = mp_obj_str_get_str(file_name);

    mp_vfs_mount_t * mp_vfs = mp_vfs_lookup_path(p_in, &p_out);

    fs_user_mount_t * vfs = (fs_user_mount_t *) mp_vfs->obj;

    FATFS fatfs = vfs->fatfs;
    FIL fp;
    UINT n;

    FRESULT res = f_open(&fatfs, &fp, p_in, FA_WRITE | FA_CREATE_ALWAYS);

    if (res != FR_OK) 
    {
        printf("Unable to open File :%d \n", res);
        return mp_const_none;
    }
    else
    {
        printf("%s open ok !\n", p_in);
    }

    CCA02M2_AUDIO_IN_Record(CCA02M2_AUDIO_INSTANCE, (uint8_t *) pyb_sai_obj.PDM_Buffer, AUDIO_IN_BUFFER_SIZE);

    static uint32_t fille_size = 0;
    ts = HAL_GetTick();
    printf("Start TS : %ld\n", ts);

    uint32_t queue_size = IFL_DEQUE_SIZE(&pyb_sai_obj.audio_queue);
    
    while((!end_record ) || (queue_size != 0))
    {   
        queue_size = IFL_DEQUE_SIZE(&pyb_sai_obj.audio_queue);

        if(queue_size > 0)
        {   
            printf("%ld %ld %ld\n", nb_write * N_MS_PER_INTERRUPT, queue_size, HAL_GetTick());

            for (uint16_t i = 0; i < queue_size; i++)
            {
                pcm_buffer_t * data = NULL;
                
                __disable_irq();
                IFL_DEQUE_FRONT(&pyb_sai_obj.audio_queue, data);
                __enable_irq();
                // pyb_sai_obj.PCM_Buffer[i % PCM_BUFFER_SIZE] = *data;

                // if((i % PCM_BUFFER_SIZE == 0 ) && (i > 0))
                {                  
                    res = f_write(&fp, (uint8_t *) data->PCM_Buffer, PCM_BUFFER_SIZE * sizeof(uint16_t), &n);
                    __disable_irq();
                    IFL_DEQUE_POP_FRONT(&pyb_sai_obj.audio_queue);
                    __enable_irq();
                    fille_size += n;

                    if (res != FR_OK) 
                    {
                        printf("Unable to wrie File :%d \n", res);
                        break;
                    }
                    else
                    {
                        // printf(" nb write : %d \n", n);
                    }

                }
            }
        } 
        __WFI();
    }

    
    f_close(&fp);

    printf("End Record %s close , size : %ld\n", p_in, fille_size);

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