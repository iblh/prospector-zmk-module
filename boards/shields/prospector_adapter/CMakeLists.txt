if(CONFIG_SHIELD_PROSPECTOR_ADAPTER)
  zephyr_library()
  file(GLOB font_sources src/fonts/*.c)
  if(CONFIG_PROSPECTOR_TOUCH_UI)
    file(GLOB_RECURSE build_info_inputs CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/*.h
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/*.c
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/*.c
      ${ZMK_CONFIG}/*)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${build_info_inputs} ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/generate_build_info.py)
    execute_process(COMMAND ${PYTHON_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/generate_build_info.py
      --module ${CMAKE_CURRENT_SOURCE_DIR}/../../..
      --config ${ZMK_CONFIG}
      --autoconfig ${PROJECT_BINARY_DIR}/.config
      --zmk ${CMAKE_SOURCE_DIR}/..
      --zephyr ${ZEPHYR_BASE}
      --output ${CMAKE_CURRENT_BINARY_DIR}/prospector_build_info.h
      COMMAND_ERROR_IS_FATAL ANY)
    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/compress_lvgl_fonts.py ${font_sources})
    set(packed_fonts "${CMAKE_CURRENT_BINARY_DIR}/packed_fonts")
    set(font_options "")
    if(CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE)
      list(APPEND font_options --uppercase-layer-fonts)
    endif()
    execute_process(COMMAND ${PYTHON_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/compress_lvgl_fonts.py
      ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts ${packed_fonts} ${font_options}
      COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB font_sources ${packed_fonts}/*.c)
  endif()
  zephyr_library_sources(${ZEPHYR_BASE}/misc/empty_file.c)
  zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
  zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
  zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
  zephyr_library_include_directories(${ZEPHYR_CURRENT_CMAKE_DIR}/include)
  zephyr_library_include_directories(include)
  zephyr_library_sources(src/brightness.c)
  zephyr_library_sources_ifdef(CONFIG_PROSPECTOR_TOUCH_UI src/touch_settings.c)
  zephyr_library_sources_ifdef(CONFIG_PROSPECTOR_TOUCH_UI src/device_screen.c src/device_service.c)
  zephyr_library_sources(src/custom_status_screen.c)
  zephyr_library_sources(src/display_rotate_init.c)

  if(CONFIG_PROSPECTOR_SHOW_MODIFIERS)
    zephyr_library_sources(src/modifier_order.c)
  endif()

  if(CONFIG_PROSPECTOR_TOUCH_UI)
    # Keep each layout's widget/listener state alive while screens are switched.
    # The original layouts export the same widget function names, so namespace
    # the overlapping functions per translation unit without editing their UI.
    foreach(layout classic field operator)
      file(GLOB layout_sources src/layouts/${layout}/*.c)
      set_source_files_properties(${layout_sources} PROPERTIES COMPILE_DEFINITIONS
        "zmk_display_status_screen=prospector_screen_${layout};zmk_widget_output_init=${layout}_output_init;zmk_widget_output_obj=${layout}_output_obj;zmk_widget_modifier_indicator_init=${layout}_modifier_init;zmk_widget_modifier_indicator_obj=${layout}_modifier_obj;zmk_widget_modifier_indicator_set_compact=${layout}_modifier_set_compact;zmk_widget_battery_circles_init=${layout}_battery_circles_init;zmk_widget_battery_circles_obj=${layout}_battery_circles_obj;battery_circles_battery_update_cb=${layout}_battery_update_cb;battery_circles_connection_update_cb=${layout}_connection_update_cb")
      zephyr_library_sources(${layout_sources})
    endforeach()
  elseif(CONFIG_PROSPECTOR_STATUS_SCREEN_CLASSIC)
    file(GLOB layout_sources src/layouts/classic/*.c)
    list(FILTER layout_sources EXCLUDE REGEX "status_screen\\.c$")
    zephyr_library_sources(${layout_sources})
  elseif(CONFIG_PROSPECTOR_STATUS_SCREEN_RADII)
    file(GLOB layout_sources src/layouts/radii/*.c)
    list(FILTER layout_sources EXCLUDE REGEX "status_screen\\.c$")
    zephyr_library_sources(${layout_sources})
  elseif(CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD)
    file(GLOB layout_sources src/layouts/field/*.c)
    list(FILTER layout_sources EXCLUDE REGEX "status_screen\\.c$")
    zephyr_library_sources(${layout_sources})
  elseif(CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR)
    file(GLOB layout_sources src/layouts/operator/*.c)
    list(FILTER layout_sources EXCLUDE REGEX "status_screen\\.c$")
    zephyr_library_sources(${layout_sources})
  endif()

  zephyr_library_sources(${font_sources})
endif()
