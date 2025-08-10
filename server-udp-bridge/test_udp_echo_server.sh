#!/bin/bash

# Простой UDP echo сервер для тестирования
python3 -c "
import socket
import sys

# Создаем UDP сокет
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('localhost', 5060))

print('UDP echo server listening on localhost:5060')
print('Press Ctrl+C to stop')

try:
    while True:
        data, addr = sock.recvfrom(1024)
        print(f'Received from {addr}: {data.decode()}')
        
        # Echo back the data
        response = f'Echo: {data.decode()}'
        sock.sendto(response.encode(), addr)
        print(f'Sent back to {addr}: {response}')
        
except KeyboardInterrupt:
    print('\nShutting down UDP echo server')
    sock.close()
"
