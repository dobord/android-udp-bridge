# Legacy UDP Bridge Sources

This directory will host deprecated legacy UDP bridge implementation files during Phase 4 of the udp2tcp migration.

Planned moves (not yet relocated to avoid large diff):
- udp_listener.c/.h
- udp_bridge_protocol.c/.h
- client_manager.c/.h
- tcp_connection_manager.c/.h
- udp_bridge_service_jni.c
- test support: udp_bridge_test.c, test_tcp_connection_manager.c, test_client_manager.c

They remain in the parent `jni/` folder until final validation of the udp2tcp adapter across all ABIs. After validation:
1. Physically move sources here.
2. Update `CMakeLists.txt` to reference them only when `USE_UDP2TCP=OFF` (already guarded).
3. Remove obsolete test binaries and adapt new udp2tcp tests.

Do not add new functionality here; only minimal maintenance until removal.
