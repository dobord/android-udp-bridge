cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Устанавливаем пути для поиска библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# Путь к нашей расширенной SSH библиотеке
set(LIBSSH_ADVANCED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/libssh_advanced)

# Создаем статическую библиотеку расширенного SSH stub
add_library(ssh_advanced STATIC
    ${LIBSSH_ADVANCED_DIR}/ssh_advanced_stub.c
)

# Настройка include директорий для расширенной библиотеки
target_include_directories(ssh_advanced PUBLIC ${LIBSSH_ADVANCED_DIR}/include)

# Создаем нашу основную библиотеку
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
)

# Добавляем include директории
target_include_directories(ssh_tunnel PRIVATE ${LIBSSH_ADVANCED_DIR}/include)

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки
target_link_libraries(ssh_tunnel
    ssh_advanced
    ${log-lib}
)

# Добавляем флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra -DHAVE_LIBSSH_ADVANCED)