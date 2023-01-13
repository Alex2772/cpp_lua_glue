cmake_minimum_required(VERSION 3.0)

project(lua)

SET(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lua-5.4.4")

include(lua.cmake)

if (PARENT_DIRECTORY)
  set(LUA_TARGET lua PARENT_SCOPE)
  set(LUA_LIBRARIES liblua PARENT_SCOPE)
endif()