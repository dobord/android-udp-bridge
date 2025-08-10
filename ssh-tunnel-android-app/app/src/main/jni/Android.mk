LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Define the library name
LIBRARY_NAME := ssh_tunnel

# Specify the source files
LOCAL_SRC_FILES := ssh_tunnel.c udp_bridge_protocol.c udp_bridge_test.c client_manager.c

# Include headers
LOCAL_C_INCLUDES := $(LOCAL_PATH)

# Include required libraries
LOCAL_LDLIBS := -lssh -llog -pthread

# Build the shared library
include $(BUILD_SHARED_LIBRARY)