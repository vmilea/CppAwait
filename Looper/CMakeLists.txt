file (GLOB _src_cxx *.cpp)
file (GLOB _src_h *.h)
file (GLOB _public_h ../include/Looper/*.h)

source_group ("Sources" FILES ${_src_cxx} ${_src_h})
source_group ("Include" FILES ${_public_h})

add_precompiled_header (stdafx.h _src_cxx)

add_library (looper ${_src_cxx} ${_src_h} ${_public_h})
