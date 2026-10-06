cmake_minimum_required(VERSION 3.16)
project(snap_preview C)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(PASSPORT_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PASSPORT_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(snap_preview preview.c
    "${PASSPORT_ROOT}/main/snap_ui.c" "${PASSPORT_ROOT}/main/snap_model.c"
    "${PASSPORT_ROOT}/main/snap_session.c" "${PASSPORT_ROOT}/main/snap_chart.c"
    "${PASSPORT_ROOT}/assets/fonts/snap_font_16.c"
    "${PASSPORT_ROOT}/assets/fonts/snap_font_22.c")
target_include_directories(snap_preview PRIVATE "${PASSPORT_ROOT}/main")
target_link_libraries(snap_preview PRIVATE lvgl m)
target_compile_options(snap_preview PRIVATE -UNDEBUG)

# Interactive browser bridge uses the same firmware UI and pure game core.
add_library(snap_host SHARED
    "${PASSPORT_ROOT}/tools/snap_debug/host.c"
    "${PASSPORT_ROOT}/main/snap_ui.c" "${PASSPORT_ROOT}/main/snap_model.c"
    "${PASSPORT_ROOT}/main/snap_session.c" "${PASSPORT_ROOT}/main/snap_chart.c" "${PASSPORT_ROOT}/main/snap_debug.c"
    "${PASSPORT_ROOT}/assets/fonts/snap_font_16.c"
    "${PASSPORT_ROOT}/assets/fonts/snap_font_22.c")
target_include_directories(snap_host PRIVATE "${PASSPORT_ROOT}/main")
target_link_libraries(snap_host PRIVATE lvgl m)
target_compile_options(snap_host PRIVATE -Wall -Wextra -Werror -UNDEBUG)
