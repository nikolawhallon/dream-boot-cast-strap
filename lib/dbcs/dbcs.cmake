# dbcs: the kit's renderer library (see renderer.h). In a project's CMakeLists.txt:
#
#   include($ENV{DC_KIT}/lib/dbcs/dbcs.cmake)
#   dbcs_use(game)
#
# Compiles the library into the target, lets it #include "dbcs/renderer.h", and on the
# Dreamcast adds what the PowerVR path needs: GLdc's headers (to reuse the textures GLdc
# uploaded) and the --wrap link flags that let it add its polygons to GLdc's frame.

set(DBCS_DIR ${CMAKE_CURRENT_LIST_DIR})

function(dbcs_use target)
    target_sources(${target} PRIVATE
        ${DBCS_DIR}/renderer.cpp
        ${DBCS_DIR}/renderer_dc.cpp)
    target_include_directories(${target} PRIVATE ${DBCS_DIR}/..)

    if(PLATFORM_DREAMCAST)
        target_include_directories(${target} PRIVATE ${SIMULANT_SOURCE_DIR}/deps/libgl/include)
        target_link_options(${target} PRIVATE
            -Wl,--wrap=pvr_scene_begin -Wl,--wrap=pvr_list_begin -Wl,--wrap=pvr_scene_finish)
    endif()
endfunction()
