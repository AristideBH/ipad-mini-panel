cmake_minimum_required(VERSION 3.16)
project(ipad_panel C)

# sim   : fenêtre SDL sur le PC, pour développer l'interface
# fbdev : /dev/fb0 + /dev/input/event0, pour l'iPad (ou un PC en console)
set(PANEL_TARGET "sim" CACHE STRING "sim ou fbdev")
set_property(CACHE PANEL_TARGET PROPERTY STRINGS sim fbdev)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(PANEL_TARGET STREQUAL "sim")
    set(PANEL_DEFS PANEL_SIM=1 PANEL_FBDEV=0)
elseif(PANEL_TARGET STREQUAL "fbdev")
    set(PANEL_DEFS PANEL_SIM=0 PANEL_FBDEV=1)
else()
    message(FATAL_ERROR "PANEL_TARGET doit valoir sim ou fbdev")
endif()

# LVGL, épinglé. Le lv_conf.h de ce dossier est pris par LV_CONF_PATH.
include(FetchContent)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
    GIT_SHALLOW    TRUE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)
target_compile_definitions(lvgl PUBLIC ${PANEL_DEFS})

add_executable(panel src/main.c src/ui.c src/ha.c)
target_compile_definitions(panel PRIVATE ${PANEL_DEFS})
target_compile_options(panel PRIVATE -Wall -Wextra)
target_link_libraries(panel PRIVATE lvgl)

find_package(Threads REQUIRED)
target_link_libraries(panel PRIVATE Threads::Threads m)

if(PANEL_TARGET STREQUAL "sim")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(panel PRIVATE ${SDL2_LIBRARIES})
endif()

# Binaire autonome pour l'iPad : aucune dépendance aux bibliothèques d'Alpine.
option(PANEL_STATIC "Lier statiquement" OFF)
if(PANEL_STATIC)
    target_link_options(panel PRIVATE -static)
endif()
