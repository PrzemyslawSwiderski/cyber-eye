idf_component_register(SRCS "main.cpp"
                    PRIV_REQUIRES esp_wifi nvs_flash gen_bmgr_codes esp_driver_tsens
                    INCLUDE_DIRS ".")
