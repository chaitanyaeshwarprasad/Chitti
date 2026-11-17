idf_component_register(
    SRCS "main.c" "board.c" "face.c" "audio.c" "net.c" "usb_setup.c" "anim.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_spi
                  esp_wifi esp_netif esp_event nvs_flash esp_http_server json lwip esp_timer esp_pm esp_driver_usb_serial_jtag esp_partition spi_flash esp_driver_tsens)

# Fast float math for the face renderer (sqrt without errno handling, fused ops).
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
