file(GLOB_RECURSE MY_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "framework/*.c"
    "framework/*.cc"
    "framework/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)

set(MY_INCS
    "."
    "hal"
)

idf_component_register(
    SRCS 
        "main.cpp"
        ${MY_SRCS}
    INCLUDE_DIRS 
        ${MY_INCS}
    EMBED_FILES
        "assets/sfx/boot_sfx.bin"
    EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
)

# LVGL 的 -Werror=all 会漏进主工程, 导致 app 代码警告变错误
# dev 分支迭代期先放宽, 修复后再收紧
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
