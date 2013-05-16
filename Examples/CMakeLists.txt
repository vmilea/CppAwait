file (GLOB _src_cxx *.cpp)
file (GLOB _src_h *.h)
file (GLOB _src_looper_cxx Looper/*.cpp)
file (GLOB _src_looper_h Looper/*.h)

source_group ("Sources" FILES ${_src_cxx} ${_src_h})
source_group ("Sources\\Looper" FILES ${_src_looper_cxx} ${_src_looper_h})

set (_src_all_cxx ${_src_cxx} ${_src_looper_cxx})

add_precompiled_header (stdafx.h _src_all_cxx)

add_executable (examples ${_src_all_cxx} ${_src_h} ${_src_looper_h})


target_link_libraries (examples cpp_await)

target_link_libraries (examples ${Boost_LIBRARIES})

if (WIN32)
    target_link_libraries (examples ws2_32 mswsock)
elseif (UNIX)
    target_link_libraries (examples rt pthread)
endif()

if (MSVC)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
endif()
