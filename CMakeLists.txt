cmake_minimum_required(VERSION 3.15)

# if cmake version < 3.14, we have to set new policy visable before project(), 
# otherwise, the MSVC flags couldnt set by set_property()
#------
# if(POLICY CMP0091)
#     message("Try to set new policy")
#     cmake_policy(SET CMP0091 NEW)
# endif(POLICY CMP0091)

project(IpcShmChannel VERSION 1.0)

#preset some define
set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/output)

message("Current project name: ${PROJECT_NAME}")
message("Current Platform is ${CMAKE_SYSTEM_NAME}")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    SET(PLF_WINDOWS 1)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        SET(PLF_DIR  "win/x64")
    else()
        set(PLF_DIR "win/x86")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    SET(PLF_LINUX 1)
    SET(PLF_DIR  linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    SET(PLF_MAC 1)
    SET(PLF_DIR mac) 
endif()

#set include dir
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/3rd/include)

#collect src file
file(GLOB_RECURSE SRC_FILE "${CMAKE_SOURCE_DIR}/src/*.cpp")



#set compiler flags
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message("Current build type is :${CMAKE_BUILD_TYPE}")
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_compile_options(-g)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message("Current build type is Release")
    add_compile_options(-O3)
endif()

#set link dir,must before add_executable()
set(LIB_DIR ${CMAKE_SOURCE_DIR}/3rd/lib/${PLF_DIR})
link_directories(${LIB_DIR})

#generate
add_executable(${PROJECT_NAME} ${SRC_FILE})
#add_library(${PROJECT_NAME} ${SRC_FILE})

#set vs src group
if(PLF_WINDOWS)
    source_group("src" FILES ${MAP_SRC_FILE})
endif()

#set /MTd or /MT
if(PLF_WINDOWS)
    message("set msvc property, for /MT or /MTD")
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# and link lib
target_link_libraries(${PROJECT_NAME} PRIVATE gtest)

#collect dynamic library file
if(PLF_WINDOWS)
    file(GLOB DLL_FILE ${LIB_DIR}/*.dll)
elseif(PLF_LINUX)
    file(GLOB DLL_FILE ${LIB_DIR}/*.so*)
endif()

#do something after build
# add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#     COMMENT "Copying ..."
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${OUTPUT_DIR}/${PLF_DIR}
#     COMMAND ${CMAKE_COMMAND} -E copy ${DLL_FILE} ${OUTPUT_DIR}/${PLF_DIR}
#     COMMAND ${CMAKE_COMMAND} -E copy ${DLL_FILE} $<TARGET_FILE_DIR:${PROJECT_NAME}>
# )


