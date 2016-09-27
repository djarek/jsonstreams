cmake_minimum_required(VERSION 2.6)
project(json_stream)
set(SRCS main.cpp
         jsmn.c)


foreach(src_file IN LISTS SRCS)
  get_filename_component(file_name ${src_file} NAME)
  set_source_files_properties(${src_file} PROPERTIES
    COMPILE_DEFINITIONS "BASE_FILE_NAME=\"${file_name}\"")
endforeach()

add_executable(json_stream ${SRCS})

target_compile_options(json_stream PUBLIC -Wall
                                               -Wextra
                                               -Weffc++
                                               -pipe
                                               -flto
                                               -pedantic
                                               -std=c++11
                                               -fno-rtti
                                               -fno-exceptions
                                               "$<$<CONFIG:Release>:-Os>")


install(TARGETS json_stream RUNTIME DESTINATION bin)
 
