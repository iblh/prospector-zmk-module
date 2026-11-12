zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PROSPECTOR_PIPELINE_PROFILE src/pipeline_profile.c)
if(CONFIG_PROSPECTOR_BLE_TX_TRACE)
  zephyr_library_sources(src/ble_tx_profile.c)
  zephyr_link_libraries(-Wl,--wrap=bt_l2cap_create_pdu_timeout -Wl,--wrap=bt_att_send)
endif()
if(CONFIG_PROSPECTOR_HCI_TRACE)
  zephyr_library_sources(src/hci_profile.c)
  zephyr_link_libraries(-Wl,--wrap=hci_acl_handle -Wl,--wrap=hci_num_cmplt_encode)
endif()
zephyr_library_sources_ifdef(CONFIG_PROSPECTOR_SCAN_AB src/scan_ab.c)
zephyr_library_sources_ifdef(CONFIG_PROSPECTOR_LEFT_SLOT_RECOVERY src/left_slot_recovery.c)

if(CONFIG_PROSPECTOR_SPLIT_SCAN_POLICY)
  if(CONFIG_PROSPECTOR_SCAN_AB)
    message(FATAL_ERROR "Scan policy and diagnostic scan AB must not be combined")
  endif()
  set(scan_original ${APPLICATION_SOURCE_DIR}/src/split/bluetooth/central.c)
  set(scan_generated ${CMAKE_CURRENT_BINARY_DIR}/scan_policy/central.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${scan_original} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_scan_policy.py)
  execute_process(COMMAND ${PYTHON_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_scan_policy.py
    --source ${scan_original} --output ${scan_generated}
    COMMAND_ERROR_IS_FATAL ANY)
  set_source_files_properties(${scan_original} TARGET_DIRECTORY app PROPERTIES HEADER_FILE_ONLY ON)
  target_sources(app PRIVATE ${scan_generated})
endif()

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
