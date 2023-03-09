set(BSP_COMMON_DIR ${CMAKE_SOURCE_DIR}/bsp/bsp_common)
set(TARGET_REQUIRED_LIBS freertos ble mbedtls)
set(mains main.c)

set(TARGET_REQUIRED_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})

set(TARGET_REQUIRED_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/ble_app.c)

list(APPEND GLOBAL_C_FLAGS -DLOW_POWER)

set(LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl702_flash_ble.ld)
generate_bin()


