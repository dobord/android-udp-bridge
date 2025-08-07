cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Устанавливаем пути для поиска библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Создаем нашу библиотеку
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
)

# Находим системные библиотеки
find_library(log-lib log)

# Добавляем пути к заголовочным файлам
target_include_directories(ssh_tunnel PRIVATE
    ${ANDROID_SYSROOT}/usr/include
)

# Связываем библиотеки
target_link_libraries(ssh_tunnel
    ${log-lib}
    ssh  # libssh library
)

# Добавляем флаги компиляции
target_compile_options(ssh_tunnel PRIVATE 
    -Wall 
    -Wextra 
    -std=c99
    -DANDROID
    -D_GNU_SOURCE
)

# Указываем, что библиотека должна быть экспортирована
set_target_properties(ssh_tunnel PROPERTIES
    LIBRARY_OUTPUT_NAME "ssh_tunnel"
)