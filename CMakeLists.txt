cmake_minimum_required(VERSION 3.18)

project(clg)

add_library(clg INTERFACE)
target_include_directories(clg INTERFACE include)

SET(LUAJIT_BUILD_EXE OFF)

add_subdirectory(3rdparty/lua)

target_link_libraries(clg INTERFACE lualib)
target_include_directories(clg INTERFACE ${LUA_DIR}/src)
