idf_component_register(
    SRCS "flipper_http.c"
    INCLUDE_DIRS "."
    REQUIRES furi toolbox storage wifi esp_http_client
)
