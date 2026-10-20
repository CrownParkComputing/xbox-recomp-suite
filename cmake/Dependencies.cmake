# Use the checked-in upstream snapshots; allow an explicit cache override.
get_filename_component(XBE_SUITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
foreach(dep zydis tomlplusplus imgui sdl3 vma spirv_headers spirv_tools glslang)
    string(TOUPPER "${dep}" upper)
    if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_${upper})
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${XBE_SUITE_ROOT}/third_party/${dep}-src" CACHE PATH "Vendored ${dep}")
    endif()
endforeach()
# DXC is a separately downloaded, hash-pinned executable dependency on Linux.
if(DEFINED ENV{DXC_SOURCE} AND
   (NOT FETCHCONTENT_SOURCE_DIR_DXC OR NOT EXISTS "${FETCHCONTENT_SOURCE_DIR_DXC}/include/dxc/dxcapi.h"))
    # Recover both an empty failed-download entry and a removed old build cache.
    set(FETCHCONTENT_SOURCE_DIR_DXC "$ENV{DXC_SOURCE}" CACHE PATH "Local DXC distribution" FORCE)
endif()
