cmake_minimum_required(VERSION 3.16)
project(a3_s_talk C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS -pthread)

add_executable(two-chat two-chat.c common.h common.c message_sender.c message_listener.c message_listener.h keyboard_reader.c keyboard_reader.h screen_printer.c screen_printer.h list.c)