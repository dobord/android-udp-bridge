LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Define the library name
LIBRARY_NAME := ssh_tunnel

# Specify the source files (udp2tcp path). Legacy sources (udp_bridge_protocol.c, udp_listener.c, client_manager.c, tcp_connection_manager.c) will be removed gradually.
LOCAL_SRC_FILES := ssh_tunnel.c udp2tcp_client_adapter.cpp \
	../../../../third_party/udp2tcp/src/common/config.cpp \
	../../../../third_party/udp2tcp/src/common/log.cpp \
	../../../../third_party/udp2tcp/src/protocol/frame.cpp \
	../../../../third_party/udp2tcp/src/c_api/c_api.cpp

# Enable udp2tcp mode
LOCAL_CFLAGS += -DUSE_UDP2TCP
LOCAL_CPPFLAGS += -std=c++20 -DUSE_UDP2TCP
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../../third_party/udp2tcp/include

# Legacy (temporarily kept for backward compatibility; comment out if old functionality is not needed)
# LOCAL_SRC_FILES += udp_bridge_protocol.c client_manager.c udp_listener.c tcp_connection_manager.c udp_bridge_test.c

# Include headers
LOCAL_C_INCLUDES := $(LOCAL_PATH)

# Include required libraries
LOCAL_LDLIBS := -lssh -llog -pthread

# Build the shared library
include $(BUILD_SHARED_LIBRARY)

# UDP Bridge library
include $(CLEAR_VARS)

# Legacy secondary library is disabled (uncomment to enable old mode)
# include $(CLEAR_VARS)
# LOCAL_MODULE := udp_bridge
# LOCAL_SRC_FILES := udp_bridge_service_jni.c udp_bridge_protocol.c client_manager.c tcp_connection_manager.c udp_listener.c
# LOCAL_C_INCLUDES := $(LOCAL_PATH)
# LOCAL_LDLIBS := -llog -pthread
# include $(BUILD_SHARED_LIBRARY)