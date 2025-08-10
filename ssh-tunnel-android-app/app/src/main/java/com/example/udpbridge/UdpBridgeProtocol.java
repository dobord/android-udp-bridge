package com.example.udpbridge;

import android.util.Log;

/**
 * UDP Bridge Protocol interface for native implementation
 * Provides Java wrapper for native UDP bridge protocol functionality
 */
public class UdpBridgeProtocol {
    private static final String TAG = "UdpBridgeProtocol";
    
    // Load native library
    static {
        try {
            System.loadLibrary("ssh_tunnel");
            Log.i(TAG, "Native library loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library", e);
        }
    }
    
    // Native method declarations
    public native int initProtocol(int localPort);
    public native void cleanupProtocol();
    public native int connectToBridge(String serverHost, int serverPort);
    public native void disconnectFromBridge();
    public native boolean isConnected();
    public native int runSelfTest();
    
    // Instance variables
    private boolean initialized = false;
    private int localPort = 0;
    private String bridgeServerHost = null;
    private int bridgeServerPort = 0;
    
    /**
     * Initialize the UDP bridge protocol
     * @param localPort Local UDP port to bind to
     * @return true if successful, false otherwise
     */
    public boolean initialize(int localPort) {
        if (initialized) {
            Log.w(TAG, "Protocol already initialized, cleaning up first");
            cleanup();
        }
        
        int result = initProtocol(localPort);
        if (result == 0) {
            initialized = true;
            this.localPort = localPort;
            Log.i(TAG, "Protocol initialized on port " + localPort);
            return true;
        } else {
            Log.e(TAG, "Failed to initialize protocol on port " + localPort);
            return false;
        }
    }
    
    /**
     * Cleanup protocol resources
     */
    public void cleanup() {
        if (initialized) {
            cleanupProtocol();
            initialized = false;
            localPort = 0;
            bridgeServerHost = null;
            bridgeServerPort = 0;
            Log.i(TAG, "Protocol cleaned up");
        }
    }
    
    /**
     * Connect to UDP bridge server
     * @param serverHost Bridge server hostname or IP
     * @param serverPort Bridge server port
     * @return true if connected successfully, false otherwise
     */
    public boolean connect(String serverHost, int serverPort) {
        if (!initialized) {
            Log.e(TAG, "Protocol not initialized");
            return false;
        }
        
        if (isConnected()) {
            Log.w(TAG, "Already connected, disconnecting first");
            disconnect();
        }
        
        int result = connectToBridge(serverHost, serverPort);
        if (result == 0) {
            this.bridgeServerHost = serverHost;
            this.bridgeServerPort = serverPort;
            Log.i(TAG, "Connected to bridge server " + serverHost + ":" + serverPort);
            return true;
        } else {
            Log.e(TAG, "Failed to connect to bridge server " + serverHost + ":" + serverPort);
            return false;
        }
    }
    
    /**
     * Disconnect from bridge server
     */
    public void disconnect() {
        if (initialized) {
            disconnectFromBridge();
            bridgeServerHost = null;
            bridgeServerPort = 0;
            Log.i(TAG, "Disconnected from bridge server");
        }
    }
    
    /**
     * Check if connected to bridge server
     * @return true if connected, false otherwise
     */
    public boolean isConnectedToBridge() {
        return initialized && isConnected();
    }
    
    /**
     * Get current status information
     * @return Status string
     */
    public String getStatus() {
        if (!initialized) {
            return "Not initialized";
        }
        
        StringBuilder status = new StringBuilder();
        status.append("Protocol initialized on port ").append(localPort);
        
        if (isConnected()) {
            status.append(", connected to ").append(bridgeServerHost).append(":").append(bridgeServerPort);
        } else {
            status.append(", not connected");
        }
        
        return status.toString();
    }
    
    /**
     * Get local UDP port
     * @return Local port number or 0 if not initialized
     */
    public int getLocalPort() {
        return localPort;
    }
    
    /**
     * Get bridge server host
     * @return Server host or null if not connected
     */
    public String getBridgeServerHost() {
        return bridgeServerHost;
    }
    
    /**
     * Get bridge server port
     * @return Server port or 0 if not connected
     */
    public int getBridgeServerPort() {
        return bridgeServerPort;
    }
    
    /**
     * Run self-test to verify protocol implementation
     * @return true if all tests pass, false otherwise
     */
    public boolean runSelfTest() {
        try {
            int result = runSelfTest();
            if (result == 0) {
                Log.i(TAG, "Self-test completed successfully");
                return true;
            } else {
                Log.e(TAG, "Self-test failed with code: " + result);
                return false;
            }
        } catch (Exception e) {
            Log.e(TAG, "Self-test threw exception", e);
            return false;
        }
    }
}
