/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "app_touchgfx.h"
#include "touch.h"
#include "fatfs.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "audio_spectrum.h"
#include "usb_msc.h"
#include "usb_audio.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

static StaticTask_t spectrumTriggerTaskControl;
static StackType_t spectrumTriggerTaskStack[640];
static StaticTask_t usbAudioTaskControl;
static StackType_t usbAudioTaskStack[640];

/* USER CODE END Variables */
osThreadId touchgfxTaskHandle;
osThreadId touchTaskHandle;
osThreadId storageTaskHandle;
osThreadId recorderTaskHandle;
osThreadId playerTaskHandle;
osThreadId spectrumTaskHandle;
osThreadId spectrumTriggerTaskHandle;
osThreadId usbMscTaskHandle;
osThreadId usbAudioTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 2 */
__weak void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
}
/* USER CODE END 2 */

/* USER CODE BEGIN 4 */
__weak void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
__weak void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  AudioRecorder_CreateResources();
  AudioPlayer_CreateResources();
  AudioSpectrum_CreateResources();
  USB_MSC_CreateResources();
  USB_Audio_CreateResources();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of the TouchGFX GUI task */
  osThreadDef(touchgfxTask, TouchGFX_Task, osPriorityNormal, 0, 1024);
  touchgfxTaskHandle = osThreadCreate(osThread(touchgfxTask), NULL);

  /* Touch sampling is independent from GUI rendering. I2C transfers complete
     through IRQ, so even a bus timeout cannot stall the TouchGFX task. */
  osThreadDef(touchTask, Touch_Task, osPriorityAboveNormal, 0, 256);
  touchTaskHandle = osThreadCreate(osThread(touchTask), NULL);

  /* Filesystem calls use polling SDMMC transfers in a low-priority task so
     GUI rendering and the touch sampler can pre-empt directory scanning. */
  /* The directory snapshot caches up to 64 entries for the scrollable Files
     view. Reserve an extra 1 KB for the scan's local snapshot and FatFs DIR. */
  osThreadDef(storageTask, SD_Storage_Task, osPriorityBelowNormal, 0, 1280);
  storageTaskHandle = osThreadCreate(osThread(storageTask), NULL);

  /* WAV writes are isolated from the GUI. Four DFSDM DMA streams fill the
     capture buffers in interrupt context; this task only drains mono chunks
     to FatFs and therefore never blocks touch sampling. */
  osThreadDef(recorderTask, AudioRecorder_Task, osPriorityNormal, 0, 1536);
  recorderTaskHandle = osThreadCreate(osThread(recorderTask), NULL);

  /* WAV decoding/refill is separated from TouchGFX. SAI1 DMA consumes a
     circular double buffer while this task performs bounded FatFs reads. */
  osThreadDef(playerTask, AudioPlayer_Task, osPriorityAboveNormal, 0, 1536);
  playerTaskHandle = osThreadCreate(osThread(playerTask), NULL);

  /* Timestamped 128-sample blocks feed a dedicated high-priority Goertzel
     detector. Its static stack does not consume the shared FreeRTOS heap. */
  osThreadStaticDef(spectrumTriggerTask, AudioSpectrum_TriggerTask,
                    osPriorityHigh, 0, 640,
                    spectrumTriggerTaskStack, &spectrumTriggerTaskControl);
  spectrumTriggerTaskHandle = osThreadCreate(osThread(spectrumTriggerTask), NULL);

  /* The normal-priority path only assembles the independent 1024-point FFT
     used by the graph, so rendering load cannot delay acoustic triggers. */
  osThreadDef(spectrumTask, AudioSpectrum_Task, osPriorityNormal, 0, 768);
  spectrumTaskHandle = osThreadCreate(osThread(spectrumTask), NULL);

  /* USB device state changes and FatFs/MSC ownership hand-off stay out of
     TouchGFX callbacks. Raw block transfers themselves are interrupt driven. */
  osThreadDef(usbMscTask, USB_MSC_Task, osPriorityNormal, 0, 512);
  usbMscTaskHandle = osThreadCreate(osThread(usbMscTask), NULL);

  /* USB packets are decoupled from SAI by a large elastic ring. This
     high-priority task directly refills the circular SAI DMA halves;
     no USB interrupt or GUI work can modulate the physical audio clocks. */
  osThreadStaticDef(usbAudioTask, USB_Audio_Task,
                    osPriorityHigh, 0, 640,
                    usbAudioTaskStack, &usbAudioTaskControl);
  usbAudioTaskHandle = osThreadCreate(osThread(usbAudioTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

