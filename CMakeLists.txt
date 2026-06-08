cmake_minimum_required(VERSION 4.2)
project(task_server C)

set(CMAKE_C_STANDARD 11)

add_executable(task_server queue.c worker.c store.c main.c task_server.h)
