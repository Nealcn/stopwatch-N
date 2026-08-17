# libopus (xiph/opus v1.5.2) 作为 IDF 组件
# 仅编 float 实现(celt/silk/src 根目录), 供 VoiceCube 的 Opus 编码使用
file(GLOB OPUS_SRCS
    "src/*.c"
    "celt/*.c"
    "silk/*.c"
    "silk/float/*.c"
)

idf_component_register(
    SRCS ${OPUS_SRCS}
    INCLUDE_DIRS "include" "celt" "silk" "silk/float" "src"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OPUS_BUILD
    FLOATING_POINT
    USE_ALLOCA
)
