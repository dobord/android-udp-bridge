LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Define the library name
LIBRARY_NAME := ssh_tunnel

# Specify the source files
LOCAL_SRC_FILES := ssh_tunnel.c

# Include the libssh library
LOCAL_LDLIBS := -lssh

# Build the shared library
include $(BUILD_SHARED_LIBRARY)