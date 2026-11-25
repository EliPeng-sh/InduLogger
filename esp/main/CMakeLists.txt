idf_component_register(SRCS "app_main.c" "link_task.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_timer
                                     spi_flash coproc_proto)
