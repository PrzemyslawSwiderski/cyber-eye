# Board information output
message(STATUS "Selected Board: esp32_p4_nano")
message(STATUS "Board Path: /home/pswidersk/REPOS/cyber-eye/components/esp32_p4_nano")

idf_component_register(
    SRC_DIRS "." "../esp32_p4_nano"
    INCLUDE_DIRS "." "../esp32_p4_nano"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
