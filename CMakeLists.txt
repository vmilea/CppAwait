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

if (MSVC10 OR MINGW)
    # MSVC10 & regular MINGW don't provide chrono & thread, fallback to Boost
    set (_boost_deps chrono coroutine context thread system)

    # mingw-gcc fails to link boost::thread
    if (WIN32 AND CMAKE_COMPILER_IS_GNUCXX)
        add_definitions (-DBOOST_THREAD_USE_LIB)
    endif()
else()
    set (_boost_deps coroutine context thread system)
endif()

message ("Boost dependencies: ${_boost_deps}")

find_package (Boost 1.57.0 REQUIRED COMPONENTS ${_boost_deps})

message ("Using Boost ${Boost_LIB_VERSION} from ${Boost_INCLUDE_DIRS}")
include_directories (${Boost_INCLUDE_DIRS})

# disable Boost autolinking
add_definitions (-DBOOST_ALL_NO_LIB)

#
# OpenSSL
#

find_package (OpenSSL)

if (OPENSSL_FOUND)
    message ("Using OpenSSL ${OPENSSL_VERSION} from ${OPENSSL_INCLUDE_DIR}")
    include_directories (${OPENSSL_INCLUDE_DIR})

	# enable Flickr example
	add_definitions (-DHAVE_OPENSSL)
endif()

#
# defs
#

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions (--std=gnu++0x -Wno-unused-parameter)
elseif (MSVC)
    add_definitions (/wd4100 /wd4127 /wd4913)
endif()

#
# paths
#

set (CMAKE_INCLUDE_CURRENT_DIR TRUE)

include_directories (include)

add_subdirectory (CppAwait)
add_subdirectory (Examples)
