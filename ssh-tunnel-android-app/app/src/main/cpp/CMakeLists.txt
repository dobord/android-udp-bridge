cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Устанавливаем пути для поиска библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# Путь к нашей заглушке libssh
set(LIBSSH_STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/libssh)

# Создаем статическую библиотеку libssh заглушку
add_library(ssh_stub STATIC
    ${LIBSSH_STUB_DIR}/libssh_stub.c
)

# Настройка include директорий для заглушки
target_include_directories(ssh_stub PUBLIC ${LIBSSH_STUB_DIR}/include)

# Создаем нашу основную библиотеку
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
)

# Добавляем include директории
target_include_directories(ssh_tunnel PRIVATE ${LIBSSH_STUB_DIR}/include)

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки
target_link_libraries(ssh_tunnel
    ssh_stub
    ${log-lib}
)

# Добавляем флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra -DHAVE_LIBSSH)