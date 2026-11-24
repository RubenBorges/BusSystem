cmake_minimum_required(VERSION 3.30)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(DesignedSystem LANGUAGES CXX)

add_subdirectory(src)

install(TARGETS DesignedSystem DESTINATION .)
install (FILES "conf/bql.conf" DESTINATION .)
