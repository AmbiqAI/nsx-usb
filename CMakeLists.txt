if(NOT DEFINED NSX_BOARD_FLAGS_TARGET)
    message(FATAL_ERROR "Board must define NSX_BOARD_FLAGS_TARGET before nsx-usb is added.")
endif()
if(NOT DEFINED NSX_SOC_FAMILY)
    message(FATAL_ERROR "NSX_SOC_FAMILY must be defined before nsx-usb is added.")
endif()
if(NOT DEFINED NSX_AMBIQSUITE_ROOT)
    message(FATAL_ERROR "NSX_AMBIQSUITE_ROOT must be defined before nsx-usb is added.")
endif()

# ---- Build TinyUSB from the ambiqsuite-r5 SDK ----
set(NSX_TINYUSB_ROOT "${NSX_AMBIQSUITE_ROOT}/third_party/tinyusb/source/src")
set(NSX_TINYUSB_SDK  "${NSX_AMBIQSUITE_ROOT}/src/usb")

if(NOT TARGET nsx_tinyusb)
    add_library(nsx_tinyusb STATIC
        ${NSX_TINYUSB_SDK}/tusb.c
        ${NSX_TINYUSB_SDK}/usbd.c
        ${NSX_TINYUSB_SDK}/usbd_control.c
        ${NSX_TINYUSB_SDK}/cdc_device_ns.c
        ${NSX_TINYUSB_SDK}/dcd_apollo5.c
        ${NSX_TINYUSB_SDK}/tusb_fifo.c
        ${NSX_TINYUSB_SDK}/vendor_device.c
        # Descriptor and callback overrides compiled here so the linker
        # always sees the strong definitions in the same archive as the
        # weak-symbol call sites, avoiding --gc-sections stripping.
        ${CMAKE_CURRENT_LIST_DIR}/src/overrides/nsx_usb_overrides.c
        ${CMAKE_CURRENT_LIST_DIR}/src/overrides/nsx_usb_descriptors.c
    )
    target_include_directories(nsx_tinyusb PUBLIC
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}>
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}/common>
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}/osal>
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}/class/cdc>
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}/class/vendor>
        $<BUILD_INTERFACE:${NSX_TINYUSB_ROOT}/device>
        # tusb_config.h lives here:
        $<BUILD_INTERFACE:${NSX_AMBIQSUITE_ROOT}/src>
        # ns_usb.h compat shim needed by cdc_device_ns.c:
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/includes-api>
    )
    target_link_libraries(nsx_tinyusb PUBLIC
        ${NSX_BOARD_FLAGS_TARGET}
        nsx_soc_hal
    )
    # TinyUSB requires CFG_TUSB_MCU to be defined.
    if(NSX_SOC_FAMILY STREQUAL "apollo5b" OR NSX_SOC_FAMILY STREQUAL "apollo510" OR NSX_SOC_FAMILY STREQUAL "apollo510b")
        target_compile_definitions(nsx_tinyusb PUBLIC CFG_TUSB_MCU=9002)
    elseif(NSX_SOC_FAMILY STREQUAL "apollo4p" OR NSX_SOC_FAMILY STREQUAL "apollo4l")
        target_compile_definitions(nsx_tinyusb PUBLIC CFG_TUSB_MCU=9001)
    endif()
    set_target_properties(nsx_tinyusb PROPERTIES EXPORT_NAME tinyusb)
    add_library(nsx::tinyusb ALIAS nsx_tinyusb)
endif()

# ---- nsx-usb module ----
add_library(nsx_usb STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_usb.c
)
set_target_properties(nsx_usb PROPERTIES EXPORT_NAME usb)
add_library(nsx::usb ALIAS nsx_usb)

target_link_libraries(nsx_usb PUBLIC
    ${NSX_BOARD_FLAGS_TARGET}
    nsx_soc_hal
    nsx_core
    nsx_harness
    nsx_utils
    nsx_tinyusb
)

target_include_directories(nsx_usb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/includes-api>
    $<BUILD_INTERFACE:${NSX_ROOT}/modules/nsx-core/includes-api>
    $<BUILD_INTERFACE:${NSX_ROOT}/modules/nsx-harness/includes-api>
    $<BUILD_INTERFACE:${NSX_ROOT}/modules/nsx-utils/includes-api>
    $<INSTALL_INTERFACE:include>
)

install(DIRECTORY includes-api/ DESTINATION include)
install(TARGETS nsx_usb nsx_tinyusb EXPORT nsxTargets)
