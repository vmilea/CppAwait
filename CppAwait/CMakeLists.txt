file (GLOB _src_cxx *.cpp)
file (GLOB _src_h *.h)
file (GLOB _public_h ../include/CppAwait/*.h)
file (GLOB _public_misc_h  ../include/CppAwait/misc/*.h)
file (GLOB _public_impl_h  ../include/CppAwait/impl/*.h)

source_group ("Sources" FILES ${_src_cxx} ${_src_h})
source_group ("Include" FILES ${_public_h})
source_group ("Include\\misc" FILES ${_public_misc_h})
source_group ("Include\\impl" FILES ${_public_impl_h})

add_precompiled_header (stdafx.h _src_cxx)

add_library (cpp_await ${_src_cxx} ${_src_h} ${_public_h} ${_public_misc_h} ${_public_impl_h})
