cmake_minimum_required(VERSION 3.8.2 FATAL_ERROR)
project(basisu VERSION 1.0.5 LANGUAGES C CXX)

# Add property to allow making folders for basis in IDEs.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)  

option(BUILD_SHARED_LIBS "Build Shared Libraries" ON)
option(BUILD_TESTS "Build test applications" ON)
option(BASISU_BUILD_TOOL "Build the tool" ON)
option(BASISU_INSTALL "Installs basis universal" ON)
option(BASISU_INSTALL_TOOL "Installs the tool" ON)

# GNU filesystem conventions
include(GNUInstallDirs)

# Windows build shared libraries
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(Threads REQUIRED)

add_subdirectory(src)

if(BUILD_TESTS)
	include(CTest)
endif()
