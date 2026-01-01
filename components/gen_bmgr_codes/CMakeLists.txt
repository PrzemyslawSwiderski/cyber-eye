# Board information output
message(STATUS "Selected Board: esp32_p4_function_ev")
message(STATUS "Board Path: /home/pswidersk/REPOS/cyber-eye/managed_components/espressif__esp_board_manager/boards/esp32_p4_function_ev")

idf_component_register(
    SRC_DIRS "." "../../managed_components/espressif__esp_board_manager/boards/esp32_p4_function_ev"
    INCLUDE_DIRS "." "../../managed_components/espressif__esp_board_manager/boards/esp32_p4_function_ev"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
