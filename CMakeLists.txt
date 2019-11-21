cmake_minimum_required(VERSION 3.1...3.15)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(Sentinel)

set (PROGRAM_VERSION_MAJOR 0)
set (PROGRAM_VERSION_MINOR 1)
set (PROGRAM_VERSION_PATCH 0)

configure_file (
  "${PROJECT_SOURCE_DIR}/ProgramConfig.h.in"
  "${PROJECT_SOURCE_DIR}/ProgramConfig.h")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(project_sources
  main.cpp
  civetweb/CivetServer.cpp
  civetweb/civetweb.c
  sqlite3/sqlite3.c)

set(project_headers
  civetweb/CivetServer.h
  civetweb/civetweb.h
  sqlite3/sqlite3.h
  sqlite3/sqlite3ext.h)

#=====================
# ..:: Visual Studio Setup ::..
#=====================

add_executable(${PROJECT_NAME}
  ${project_sources}
  ${project_headers})

# Setup flags for compilers
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
  -Wall 
  #-DUSE_WEBSOCKET 
  #-DNO_FILES
  >
  $<$<CXX_COMPILER_ID:MSVC>:
  /W4 
  #/DUSE_WEBSOCKET 
  #/DNO_FILES
  >
)

#target_link_libraries(${PROJECT_NAME} PRIVATE )

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${PROJECT_SOURCE_DIR}
  PRIVATE
  sqlite3
  civetweb)

# auto copy dlls if on windows
if(MSVS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:${PROJECT_NAME}> )
endif()
