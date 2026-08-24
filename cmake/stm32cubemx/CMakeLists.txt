cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	USE_HAL_DRIVER 
	STM32F769xx
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_DEVICE/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_DEVICE/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../TouchGFX/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../TouchGFX/target/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/../../TouchGFX/target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/otm8009a
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/STM32F769I-Discovery
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/wm8994
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32F7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/App/fatfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/user_diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/freertos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/audio_codec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/audio_player.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/audio_recorder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/audio_spectrum.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usb_msc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usb_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_tx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_upsampler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_iir_upsampler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_minphase_fir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_wls_fir_coefficients.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdif_wls_listen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_DEVICE/App/usbd_desc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_DEVICE/App/usbd_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../USB_DEVICE/Target/usbd_conf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_bot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_scsi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/touch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/dma2d.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/dsihost.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/eth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/fmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/hdmi_cec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/ltdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/quadspi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sai.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sdmmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spdifrx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usb_otg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/wwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f7xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f7xx_hal_timebase_tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32f769xx.s
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/otm8009a/otm8009a.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/otm8009a/otm8009a_reg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/Components/wm8994/wm8994.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/STM32F769I-Discovery/stm32f769i_discovery_audio.c
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32f7xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_adc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_crc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma2d.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dfsdm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dsi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_eth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_fmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_nor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_nand.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sdram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_ltdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_ltdc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_qspi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rtc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sai.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sai_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_sdmmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_mmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_spdifrx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_spi_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pcd_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_wwdg.c
)

# Drivers Midllewares


set(FreeRTOS_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/croutine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/timers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS/cmsis_os.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1/port.c
)
set(FatFs_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FatFs/src/option/syscall.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    FreeRTOS	FatFs	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)
# Validate that STM32CubeMX code is compatible with C++ standard
if(CMAKE_CXX_STANDARD LESS 11)
    message(ERROR "Generated code requires C++11 or higher")
endif()

# Create FreeRTOS static library
add_library(FreeRTOS OBJECT)
target_sources(FreeRTOS PRIVATE ${FreeRTOS_Src})
target_link_libraries(FreeRTOS PUBLIC stm32cubemx)

# Create FatFs static library
add_library(FatFs OBJECT)
target_sources(FatFs PRIVATE ${FatFs_Src})
target_link_libraries(FatFs PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
