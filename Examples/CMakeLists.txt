file (GLOB _src_cxx *.cpp)
file (GLOB _src_h *.h)

source_group ("Sources" FILES ${_src_cxx} ${_src_h})

add_precompiled_header (stdafx.h _src_cxx)

add_executable (examples ${_src_cxx} ${_src_h})


target_link_libraries (examples cpp_await looper)

target_link_libraries (examples ${Boost_LIBRARIES})

if (WIN32)
	target_link_libraries (examples ws2_32)
elseif (UNIX)
	target_link_libraries (examples rt)
endif()

if (MSVC)
	set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
endif()