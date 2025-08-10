#!/bin/bash

# Простой тест протокола UDP Bridge
# Этот скрипт создает корректное протокольное сообщение и отправляет его серверу

echo "=== Тест протокола UDP Bridge ==="

# Параметры
SERVER_HOST="localhost"
SERVER_PORT="8080"

# Создание простого протокольного сообщения для регистрации клиента
# Magic: UDPB (4 байта)
# Version: 1 (1 байт)  
# Message Type: 2 (CLIENT_REGISTER, 1 байт)
# Flags: 0 (2 байта)
# Client ID: 0 (4 байта, сервер назначит)
# Payload Size: 0 (4 байта)
# Checksum: расчетный (4 байт)

# Используем printf для создания бинарных данных
printf "UDPB\x01\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" | nc -w5 $SERVER_HOST $SERVER_PORT | xxd

echo ""
echo "Тест завершен. Проверьте логи сервера для подтверждения."
