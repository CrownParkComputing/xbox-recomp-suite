# Linux: europe.patch as a generated array, in place of the Windows resource
set(BLACK_DXC_DIR ${XBE_DXC_DIR})  # the SDK fetches DXC and VMA

# europe.patch as kEuropePatch, written again when the patch changes
set(EUROPE_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/src/common/europe.patch)
set(EUROPE_CPP ${CMAKE_CURRENT_BINARY_DIR}/europe_patch.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EUROPE_PATCH})
file(READ ${EUROPE_PATCH} europe_hex HEX)
string(LENGTH "${europe_hex}" europe_len)
math(EXPR europe_size "${europe_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," europe_bytes "${europe_hex}")
file(CONFIGURE OUTPUT ${EUROPE_CPP} CONTENT "// generated from src/common/europe.patch\n#include <cstddef>\n#include <cstdint>\nnamespace app {\nextern const uint8_t kEuropePatch[] = {${europe_bytes}};\nextern const size_t kEuropePatchSize = ${europe_size};\n}  // namespace app\n")

# the Linux additions to an image's executable
function(black_linux_image target)
    target_sources(${target} PRIVATE ${EUROPE_CPP})
endfunction()
