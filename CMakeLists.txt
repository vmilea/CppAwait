cmake_minimum_required (VERSION 2.8)

project (CppAwaitSolution CXX)

#
# modules
#

set (CMAKE_MODULE_PATH ${CppAwaitSolution_SOURCE_DIR}/cmake_modules ${CMAKE_MODULE_PATH})

include (precompiled_header)
include (enable_max_warning_level)

#
# Boost
#

set (Boost_USE_STATIC_RUNTIME OFF)
set (Boost_USE_STATIC_LIBS ON)

find_package (Boost 1.52.0 REQUIRED COMPONENTS context system)
include_directories (${Boost_INCLUDE_DIRS})

# disable Boost autolinking
add_definitions (-DBOOST_ALL_NO_LIB)

#
# defs
#

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions (--std=gnu++0x -Wno-unused-parameter)
elseif (MSVC)
    add_definitions (/wd4100)
endif()

#
# paths
#

set (CMAKE_INCLUDE_CURRENT_DIR TRUE)

include_directories (include)

add_subdirectory (CppAwait)
add_subdirectory (Examples)
