idf_component_register(
    SRCS "main.c" "snap_model.c" "snap_detector.c" "snap_session.c" "snap_ui.c"
         "../assets/fonts/snap_font_16.c" "../assets/fonts/snap_font_22.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hw_support nvs_flash
)
