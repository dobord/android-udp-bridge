# Desktop test CLI for android-udp-bridge

This tiny CLI reuses the same udp2tcp client adapter sources as the Android app, and uses system libssh to test SSH forwarding locally on Linux.

Prereqs:
- libssh dev package (e.g. Debian/Ubuntu: `sudo apt-get install libssh-dev`)
- CMake 3.20+

Build:
- mkdir -p build && cd build
- cmake ..
- cmake --build . -j

Run examples:
- ./udp-bridge-cli --ssh example.com user pass 22
- ./udp-bridge-cli --forward 127.0.0.1 8000 18000 --ssh example.com user pass 22
- ./udp-bridge-cli --udp2tcp 127.0.0.1 8000 127.0.0.1 18000 127.0.0.1 9000 127.0.0.1 9001 --ssh example.com user pass 22

Notes:
- The CLI opens a test SSH direct-tcpip channel once when --forward is given; for full data proxying you should run the Android app or extend this CLI.
- udp2tcp connects to the provided local bridge (typically 127.0.0.1:<lport>) which you can forward over SSH outside or via --forward test.
