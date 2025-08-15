// Legacy full implementation of udp_bridge_service_jni migrated from parent directory.
// This file preserves original behavior for legacy build path.
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include "udp_bridge_protocol.h"
#include "client_manager.h"
#include "tcp_connection_manager.h"
#include "udp_listener.h"

#define LOG_TAG "UdpBridgeJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

static struct {
	int udp_socket;
	int tcp_socket;
	struct sockaddr_in bridge_addr;
	client_manager_t* client_manager;
	tcp_connection_manager_t* tcp_manager;
	pthread_t bridge_thread;
	pthread_t listener_thread;
	int running;
	int local_port;
	char bridge_host[256];
	int bridge_port;
	long bytes_transferred;
	int protocol_connected;
	pthread_mutex_t state_mutex;
} bridge_state = {
	.udp_socket = -1,
	.tcp_socket = -1,
	.client_manager = NULL,
	.tcp_manager = NULL,
	.running = 0,
	.local_port = 0,
	.bridge_host = {0},
	.bridge_port = 0,
	.bytes_transferred = 0,
	.protocol_connected = 0,
	.state_mutex = PTHREAD_MUTEX_INITIALIZER
};

void* bridge_thread_func(void* arg);
void* listener_thread_func(void* arg);

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_initializeBridge(JNIEnv *env, jobject thiz, jint localPort) {
	LOGI("Initializing UDP Bridge on port %d", localPort);
	pthread_mutex_lock(&bridge_state.state_mutex);
	if (bridge_state.running) {
		LOGE("Bridge already running, stopping first");
		bridge_state.running = 0;
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.local_port = localPort;
	bridge_state.udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (bridge_state.udp_socket < 0) {
		LOGE("Failed to create UDP socket: %s", strerror(errno));
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	int reuse = 1;
	setsockopt(bridge_state.udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	struct sockaddr_in local_addr = {0};
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = INADDR_ANY;
	local_addr.sin_port = htons(localPort);
	if (bind(bridge_state.udp_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
		LOGE("Failed to bind UDP socket to port %d: %s", localPort, strerror(errno));
		close(bridge_state.udp_socket);
		bridge_state.udp_socket = -1;
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.client_manager = client_manager_create(300, 1000);
	if (!bridge_state.client_manager) {
		LOGE("Failed to create client manager");
		close(bridge_state.udp_socket);
		bridge_state.udp_socket = -1;
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.bytes_transferred = 0;
	LOGI("UDP Bridge initialized successfully on port %d", localPort);
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_connectToBridgeServer(JNIEnv *env, jobject thiz, jstring host, jint port) {
	const char* host_str = (*env)->GetStringUTFChars(env, host, NULL);
	if (!host_str) return JNI_FALSE;
	LOGI("Connecting to bridge server %s:%d", host_str, port);
	pthread_mutex_lock(&bridge_state.state_mutex);
	strncpy(bridge_state.bridge_host, host_str, sizeof(bridge_state.bridge_host) - 1);
	bridge_state.bridge_port = port;
	bridge_state.tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (bridge_state.tcp_socket < 0) {
		LOGE("Failed to create TCP socket: %s", strerror(errno));
		(*env)->ReleaseStringUTFChars(env, host, host_str);
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	struct timeval timeout = { .tv_sec = 10, .tv_usec = 0 };
	setsockopt(bridge_state.tcp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(bridge_state.tcp_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	memset(&bridge_state.bridge_addr, 0, sizeof(bridge_state.bridge_addr));
	bridge_state.bridge_addr.sin_family = AF_INET;
	bridge_state.bridge_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host_str, &bridge_state.bridge_addr.sin_addr) <= 0) {
		LOGE("Invalid bridge server address: %s", host_str);
		close(bridge_state.tcp_socket);
		bridge_state.tcp_socket = -1;
		(*env)->ReleaseStringUTFChars(env, host, host_str);
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	if (connect(bridge_state.tcp_socket, (struct sockaddr*)&bridge_state.bridge_addr, sizeof(bridge_state.bridge_addr)) < 0) {
		LOGE("Failed to connect to bridge server %s:%d: %s", host_str, port, strerror(errno));
		close(bridge_state.tcp_socket);
		bridge_state.tcp_socket = -1;
		(*env)->ReleaseStringUTFChars(env, host, host_str);
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.tcp_manager = tcp_connection_manager_create();
	if (!bridge_state.tcp_manager) {
		LOGE("Failed to create TCP connection manager");
		close(bridge_state.tcp_socket);
		bridge_state.tcp_socket = -1;
		(*env)->ReleaseStringUTFChars(env, host, host_str);
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.protocol_connected = 1;
	LOGI("Connected to bridge server %s:%d", host_str, port);
	(*env)->ReleaseStringUTFChars(env, host, host_str);
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_startProtocolHandler(JNIEnv *env, jobject thiz) {
	LOGI("Starting protocol handler");
	pthread_mutex_lock(&bridge_state.state_mutex);
	if (bridge_state.running || bridge_state.udp_socket < 0 || bridge_state.tcp_socket < 0) {
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	bridge_state.running = 1;
	if (pthread_create(&bridge_state.bridge_thread, NULL, bridge_thread_func, NULL) != 0) {
		bridge_state.running = 0;
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	if (pthread_create(&bridge_state.listener_thread, NULL, listener_thread_func, NULL) != 0) {
		bridge_state.running = 0;
		pthread_join(bridge_state.bridge_thread, NULL);
		pthread_mutex_unlock(&bridge_state.state_mutex);
		return JNI_FALSE;
	}
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return JNI_TRUE;
}

static void bridge_state_stop_locked() {
	bridge_state.running = 0;
	bridge_state.protocol_connected = 0;
}

JNIEXPORT void JNICALL
Java_com_example_udpbridge_UdpBridgeService_stopBridge(JNIEnv *env, jobject thiz) {
	pthread_mutex_lock(&bridge_state.state_mutex);
	bridge_state_stop_locked();
	pthread_mutex_unlock(&bridge_state.state_mutex);
	if (bridge_state.bridge_thread) { pthread_join(bridge_state.bridge_thread, NULL); bridge_state.bridge_thread = 0; }
	if (bridge_state.listener_thread) { pthread_join(bridge_state.listener_thread, NULL); bridge_state.listener_thread = 0; }
	pthread_mutex_lock(&bridge_state.state_mutex);
	if (bridge_state.tcp_manager) { tcp_connection_manager_destroy(bridge_state.tcp_manager); bridge_state.tcp_manager = NULL; }
	if (bridge_state.tcp_socket >= 0) { close(bridge_state.tcp_socket); bridge_state.tcp_socket = -1; }
	if (bridge_state.udp_socket >= 0) { close(bridge_state.udp_socket); bridge_state.udp_socket = -1; }
	if (bridge_state.client_manager) { client_manager_destroy(bridge_state.client_manager); bridge_state.client_manager = NULL; }
	bridge_state.bytes_transferred = 0;
	pthread_mutex_unlock(&bridge_state.state_mutex);
}

JNIEXPORT jint JNICALL
Java_com_example_udpbridge_UdpBridgeService_getClientCount(JNIEnv *env, jobject thiz) {
	pthread_mutex_lock(&bridge_state.state_mutex);
	int count = bridge_state.client_manager ? client_manager_get_count(bridge_state.client_manager) : 0;
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return count;
}

JNIEXPORT jlong JNICALL
Java_com_example_udpbridge_UdpBridgeService_getBytesTransferred(JNIEnv *env, jobject thiz) {
	pthread_mutex_lock(&bridge_state.state_mutex);
	long bytes = bridge_state.bytes_transferred;
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return bytes;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_isProtocolConnected(JNIEnv *env, jobject thiz) {
	pthread_mutex_lock(&bridge_state.state_mutex);
	int connected = bridge_state.protocol_connected && bridge_state.running;
	pthread_mutex_unlock(&bridge_state.state_mutex);
	return connected ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_udpbridge_UdpBridgeService_nativeStopBridge(JNIEnv *env, jobject thiz) {
	Java_com_example_udpbridge_UdpBridgeService_stopBridge(env, thiz);
}

void* bridge_thread_func(void* arg) {
	char buffer[4096];
	while (bridge_state.running) {
		ssize_t bytes_received = recv(bridge_state.tcp_socket, buffer, sizeof(buffer), 0);
		if (bytes_received <= 0) { bridge_state.protocol_connected = 0; break; }
		udp_bridge_header_t header;
		if (protocol_parse_header(buffer, bytes_received, &header) == 0) {
			if (header.message_type == MSG_DATA && bridge_state.client_manager) {
				client_entry_t* client = client_manager_find_by_id(bridge_state.client_manager, header.client_id);
				if (client) {
					sendto(bridge_state.udp_socket, buffer + sizeof(udp_bridge_header_t), header.payload_size, 0, (struct sockaddr*)&client->client_addr, sizeof(client->client_addr));
					bridge_state.bytes_transferred += header.payload_size;
					client_manager_update_activity(bridge_state.client_manager, header.client_id);
				}
			} else if (header.message_type == MSG_CLIENT_TIMEOUT && bridge_state.client_manager) {
				client_manager_remove_client(bridge_state.client_manager, header.client_id);
			}
		}
	}
	return NULL;
}

void* listener_thread_func(void* arg) {
	char buffer[4096];
	struct sockaddr_in client_addr; socklen_t addr_len;
	while (bridge_state.running) {
		addr_len = sizeof(client_addr);
		ssize_t bytes_received = recvfrom(bridge_state.udp_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &addr_len);
		if (bytes_received <= 0) continue;
		uint32_t client_id = 0;
		if (bridge_state.client_manager) {
			client_entry_t* client = client_manager_find_by_addr(bridge_state.client_manager, &client_addr);
			if (!client) client_id = client_manager_add_client(bridge_state.client_manager, &client_addr); else { client_id = client->client_id; client_manager_update_activity(bridge_state.client_manager, client_id); }
		}
		if (bridge_state.tcp_manager && client_id > 0) {
			char protocol_buffer[4096 + sizeof(udp_bridge_header_t)];
			int msg_size = protocol_create_message(protocol_buffer, sizeof(protocol_buffer), MSG_DATA, client_id, 0, buffer, bytes_received);
			if (msg_size > 0) {
				if (tcp_connection_manager_send_data(bridge_state.tcp_manager, client_id, protocol_buffer, msg_size) > 0) {
					bridge_state.bytes_transferred += bytes_received;
				}
			}
		}
	}
	return NULL;
}
