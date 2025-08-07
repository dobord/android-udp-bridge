cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Создаем нашу библиотеку (пока без libssh для первоначальной сборки)
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
)

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки
target_link_libraries(ssh_tunnel
    ${log-lib}
)

# Добавляем флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra)