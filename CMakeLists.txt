cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBMPEG2_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_mpeg2v.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_mpeg2.c
)

SET(LIBMPEG2_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmpeg2.a
)

SET(LIBMPEG2_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libmpeg2
        "${LIBMPEG2_SRC}"
        "${LIBMPEG2_LIB}"
        []
        ""
        "${LIBMPEG2_INC}"
        ""
        "1")
