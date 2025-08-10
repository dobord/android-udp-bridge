LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Define the library name
LIBRARY_NAME := ssh_tunnel

# Specify the source files
LOCAL_SRC_FILES := ssh_tunnel.c udp_bridge_protocol.c udp_bridge_test.c client_manager.c udp_listener.c tcp_connection_manager.c

# Include headers
LOCAL_C_INCLUDES := $(LOCAL_PATH)

# Include required libraries
LOCAL_LDLIBS := -lssh -llog -pthread

# Build the shared library
include $(BUILD_SHARED_LIBRARY)

# UDP Bridge library
include $(CLEAR_VARS)

LOCAL_MODULE := udp_bridge
LOCAL_SRC_FILES := udp_bridge_service_jni.c udp_bridge_protocol.c client_manager.c tcp_connection_manager.c udp_listener.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_LDLIBS := -llog -pthread

include $(BUILD_SHARED_LIBRARY)