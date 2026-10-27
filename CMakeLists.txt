zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library()

add_subdirectory_ifdef(CONFIG_PROSPECTOR_APDS9960 drivers/sensor/apds9960)

if(CONFIG_SHIELD_PROSPECTOR_ADAPTER)

        add_subdirectory(${ZEPHYR_CURRENT_MODULE_DIR}/drivers/display)

        if(CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED)
                zephyr_library_sources(src/events/caps_word_state_changed.c)
                set_source_files_properties(
                        ${APPLICATION_SOURCE_DIR}/src/behaviors/behavior_caps_word.c
                        TARGET_DIRECTORY app
                        PROPERTIES HEADER_FILE_ONLY ON)
                target_sources(app PRIVATE src/behaviors/behavior_caps_word.c)
        endif()

        if(CONFIG_DT_HAS_ZMK_BEHAVIOR_PROSPECTOR_BRIGHTNESS_ENABLED)
                target_sources(app PRIVATE src/behaviors/behavior_prospector_brightness.c)
        endif()

        zephyr_library_sources(src/events/split_central_status_changed.c)
        zephyr_library_sources(src/split/bluetooth/central_status_changed_observer.c)

endif()

if(CONFIG_PROSPECTOR_INPUT_MODES)
  target_sources(app PRIVATE src/input_mode.c src/behaviors/behavior_input_mode.c
    src/behaviors/behavior_shortcut.c)
  zephyr_link_libraries(-Wl,--wrap=raise_zmk_position_state_changed)
endif()

if(CONFIG_PROSPECTOR_TOUCH_UI)
  zephyr_library_sources(src/peripheral_state.c)
endif()
