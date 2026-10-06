idf_component_register(
    SRCS "main.c" "snap_model.c" "snap_detector.c" "snap_session.c" "snap_ui.c" "snap_debug.c" "snap_chart.c" "snap_music.c"
         "../assets/fonts/snap_font_16.c" "../assets/fonts/snap_font_22.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hw_support nvs_flash
)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../assets/music/mountain-king.pcm" BINARY)
