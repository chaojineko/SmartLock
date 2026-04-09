set(src_dirs        .
                    src
                    ai)

set(include_dirs    include
                    ai
                    .)

set(requires
    esp-code-scanner
    esp_http_server
    esp_timer
    esp_event
    esp_wifi
    esp_netif
    esp_http_client
    tcp_transport
    mbedtls
    nvs_flash
    lwip
    spi_flash
    fb_gfx
    esp-dl
    yahboom_esp32-camera
    yahboom__mdns
    esp_driver_uart
    esp_driver_gpio
    esp_driver_i2c
    bt
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)
#idf_component_register(SRCS "app_main.cpp")
