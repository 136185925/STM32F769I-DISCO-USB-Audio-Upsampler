cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME f769)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
set(TOUCHGFX_ROOT "C:/TouchGFX/4.26.1/touchgfx" CACHE PATH
    "TouchGFX framework root")
set(TOUCHGFX_LIBRARY
    "${TOUCHGFX_ROOT}/lib/core/cortex_m7/gcc/libtouchgfx-float-abi-hard.a")
if(NOT EXISTS "${TOUCHGFX_LIBRARY}")
    message(FATAL_ERROR
        "TouchGFX 4.26.1 library not found. Set TOUCHGFX_ROOT to its touchgfx directory.")
endif()

set(CMSIS_DSP_ROOT
    "$ENV{USERPROFILE}/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS"
    CACHE PATH "CMSIS root containing DSP/Include and Lib/GCC")
set(CMSIS_DSP_LIBRARY
    "${CMSIS_DSP_ROOT}/Lib/GCC/libarm_cortexM7lfsp_math.a")
if(NOT EXISTS "${CMSIS_DSP_ROOT}/DSP/Include/arm_math.h" OR
   NOT EXISTS "${CMSIS_DSP_LIBRARY}")
    message(FATAL_ERROR
        "CMSIS-DSP Cortex-M7 library not found. Set CMSIS_DSP_ROOT to the Drivers/CMSIS directory.")
endif()

file(GLOB_RECURSE TOUCHGFX_GUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/TouchGFX/gui/src/*.cpp")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${TOUCHGFX_GUI_SOURCES}
    TouchGFX/App/app_touchgfx.c
    TouchGFX/target/STM32TouchController.cpp
    TouchGFX/target/TouchGFXGPIO.cpp
    TouchGFX/target/TouchGFXHAL.cpp
    TouchGFX/target/generated/OSWrappers.cpp
    TouchGFX/target/generated/STM32DMA.cpp
    TouchGFX/target/generated/TouchGFXConfiguration.cpp
    TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${TOUCHGFX_ROOT}/framework/include
    ${CMSIS_DSP_ROOT}/DSP/Include
    TouchGFX/gui/include
    TouchGFX/target
    TouchGFX/target/generated
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ARM_MATH_CM7
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    ${TOUCHGFX_LIBRARY}
    ${CMSIS_DSP_LIBRARY}

    # Add user defined libraries
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating HEX and BIN firmware images"
)
