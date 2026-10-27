cmake_minimum_required(VERSION 3.20)

project(prospector_operator_renderer C)

set(PROSPECTOR_ROOT "" CACHE PATH "Prospector module root")
set(LVGL_DIR "" CACHE PATH "LVGL source directory")
option(PROSPECTOR_HOST_INPUT_MODES "Render and test input-mode pages" OFF)

set(OPERATOR_LAYER_COUNT "5" CACHE STRING "Layer count")
set(OPERATOR_PROFILE_COUNT "5" CACHE STRING "BLE profile count")
set(OPERATOR_PERIPHERAL_COUNT "2" CACHE STRING "Peripheral count")

if(NOT PROSPECTOR_ROOT)
  message(FATAL_ERROR "PROSPECTOR_ROOT is required")
endif()

if(NOT LVGL_DIR)
  message(FATAL_ERROR "LVGL_DIR is required")
endif()

configure_file(${CMAKE_CURRENT_LIST_DIR}/lv_conf.h ${CMAKE_BINARY_DIR}/lv_conf.h COPYONLY)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)
target_include_directories(lvgl SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_executable(operator_renderer
  src/main.c
  src/host_state.c
  src/host_modifier_order.c
  src/host_touch_brightness.c
  src/host_device_service.c
  src/device_screen_tests.c
  src/host_peripheral_state.c
  ${PROSPECTOR_ROOT}/src/peripheral_state.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/device_screen.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/status_screen.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/touch_settings.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/modifier_indicator.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/wpm_meter.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/layer_display.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/battery_circles.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator/output.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FG_Medium_20.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FG_Medium_21.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FG_Medium_26.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/DINish_Expanded_Light_36.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FR_Medium_32.c
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/DINish_Medium_24.c
)

target_compile_definitions(operator_renderer PRIVATE
  CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR=1
  CONFIG_PROSPECTOR_TOUCH_UI=1
  ZMK_SPLIT_BLE_PERIPHERAL_COUNT=${OPERATOR_PERIPHERAL_COUNT}
  CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT=1
  CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=1
  CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE=1
  CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED=1
  PROSPECTOR_RENDER_INSTANT_BATTERY=1
  ZMK_BLE_PROFILE_COUNT=${OPERATOR_PROFILE_COUNT}
  ZMK_KEYMAP_LAYERS_LEN=${OPERATOR_LAYER_COUNT}
  PERIPHERAL_COUNT=${OPERATOR_PERIPHERAL_COUNT}
)

target_compile_options(operator_renderer PRIVATE -include stdio.h)

target_include_directories(operator_renderer PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/include
  ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/operator
  ${PROSPECTOR_ROOT}/include
)

target_link_libraries(operator_renderer PRIVATE lvgl::lvgl m)

if(PROSPECTOR_HOST_INPUT_MODES)
  target_compile_definitions(operator_renderer PRIVATE CONFIG_PROSPECTOR_INPUT_MODES=1)
endif()

option(PROSPECTOR_HOST_ALL_LAYOUTS "Create Classic and Field as well" OFF)
if(PROSPECTOR_HOST_ALL_LAYOUTS)
  target_sources(operator_renderer PRIVATE src/peripheral_screen_tests.c)
  target_compile_definitions(operator_renderer PRIVATE PROSPECTOR_HOST_ALL_LAYOUTS=1
    CONFIG_PROSPECTOR_SHOW_MODIFIERS=1 CONFIG_PROSPECTOR_SHOW_INACTIVE_MODIFIERS=1
    CONFIG_PROSPECTOR_ANIMATION_WPM_REFERENCE=80
    CONFIG_PROSPECTOR_ANIMATION_INTENSITY_DECAY_SEC=30 CONFIG_PROSPECTOR_ANIMATION_FLOW_DECAY_SEC=300)
  target_include_directories(operator_renderer PRIVATE ${LVGL_DIR}/../../../../zmk/app/include)
  foreach(layout classic field)
    file(GLOB extra_layout_sources ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/layouts/${layout}/*.c)
    set_source_files_properties(${extra_layout_sources} PROPERTIES COMPILE_DEFINITIONS
      "zmk_display_status_screen=prospector_screen_${layout};zmk_widget_output_init=${layout}_output_init;zmk_widget_output_obj=${layout}_output_obj;zmk_widget_modifier_indicator_init=${layout}_modifier_init;zmk_widget_modifier_indicator_obj=${layout}_modifier_obj;zmk_widget_modifier_indicator_set_compact=${layout}_modifier_set_compact")
    target_sources(operator_renderer PRIVATE ${extra_layout_sources})
  endforeach()
  target_sources(operator_renderer PRIVATE
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/DINish_Condensed_Semibold_20.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/DINish_Condensed_Semibold_22.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/Symbols_Semibold_32.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FG_Medium_24.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FR_Regular_48.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FR_Thin_48.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FR_Regular_30.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/FR_Regular_36.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/PPF_NarrowThin_64.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/Symbols_Regular_28.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/Symbols_Medium_28.c
    ${PROSPECTOR_ROOT}/boards/shields/prospector_adapter/src/fonts/Symbols_Bold_26.c)
endif()
