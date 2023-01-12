cmake_minimum_required(VERSION 3.18)

project(clg)

add_library(clg INTERFACE)
target_include_directories(clg INTERFACE include)

# SET(LUAJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lua/luaJit-2.1" CACHE PATH "Path of LuaJIT 2.1 source dir" FORCE)
SET(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lua/lua-5.4.4" CACHE PATH "Path of Lua 5.4 source dir" FORCE)
SET(LUAJIT_BUILD_EXE OFF)

add_subdirectory(3rdparty/lua)

# if(LUAJIT_DIR)
#     target_link_libraries(clg INTERFACE libluajit)
#     target_include_directories(clg INTERFACE ${LUAJIT_DIR}/src)
# else
if(LUA_DIR)
    target_link_libraries(clg INTERFACE lualib)
    target_include_directories(clg INTERFACE ${LUA_DIR}/src)
endif()
