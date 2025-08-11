package com.example.udpbridge;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;
import android.os.Handler;
import android.os.HandlerThread;
import android.content.Context;

import com.example.sshtunnel.SshTunnelService;

/**
 * Service for managing UDP Bridge connections with the new protocol
 */
public class UdpBridgeService extends Service {
    private static final String TAG = "UdpBridgeService";
    
    // Bridge states
    public enum BridgeState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FORWARDING,
        ERROR
    }
    
    private BridgeState currentState = BridgeState.DISCONNECTED;
    private UdpBridgeConfig config;
    private SshTunnelService sshService;
    
    // Native library
    static {
        System.loadLibrary("ssh_tunnel");
    }
    
    // Native methods for new protocol
    public native boolean initializeBridge(int localPort);
    public native boolean connectToBridgeServer(String host, int port);
    public native boolean startProtocolHandler();
    public native void nativeStopBridge();
    public native int getClientCount();
    public native long getBytesTransferred();
    public native boolean isProtocolConnected();
    
    // Background thread for bridge operations
    private HandlerThread bridgeThread;
    private Handler bridgeHandler;
    
    // Statistics
    private long totalBytesTransferred = 0;
    private int activeClients = 0;
    private long connectionStartTime = 0;
    
    // Bridge event listener interface
    public interface BridgeEventListener {
        void onStateChanged(BridgeState newState);
        void onClientConnected(int clientId);
        void onClientDisconnected(int clientId);
        void onDataTransferred(long bytes);
        void onError(String error);
    }
    
    private BridgeEventListener eventListener;
    
    public class LocalBinder extends Binder {
        public UdpBridgeService getService() {
            return UdpBridgeService.this;
        }
    }
    
    private final IBinder binder = new LocalBinder();

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "UDP Bridge Service created");
        
        config = new UdpBridgeConfig(this);
        
        // Create background thread for bridge operations
        bridgeThread = new HandlerThread("UdpBridgeThread");
        bridgeThread.start();
        bridgeHandler = new Handler(bridgeThread.getLooper());
        
        setState(BridgeState.DISCONNECTED);
    }
    
    /**
     * Set event listener for bridge events
     */
    public void setEventListener(BridgeEventListener listener) {
        this.eventListener = listener;
    }
    
    /**
     * Start UDP bridge with current configuration
     */
    public boolean startBridge() {
        if (currentState != BridgeState.DISCONNECTED) {
            Log.w(TAG, "Bridge already started or in progress");
            return false;
        }
        
        if (!config.isValid()) {
            Log.e(TAG, "Invalid configuration");
            notifyError("Invalid bridge configuration");
            return false;
        }
        
        setState(BridgeState.CONNECTING);
        
        bridgeHandler.post(() -> {
            try {
                // Initialize native bridge
                if (!initializeBridge(config.getLocalPort())) {
                    notifyError("Failed to initialize UDP bridge on port " + config.getLocalPort());
                    setState(BridgeState.ERROR);
                    return;
                }
                
                // Connect to bridge server
                if (!connectToBridgeServer(config.getBridgeHost(), config.getBridgePort())) {
                    notifyError("Failed to connect to bridge server " + 
                              config.getBridgeHost() + ":" + config.getBridgePort());
                    setState(BridgeState.ERROR);
                    return;
                }
                
                setState(BridgeState.CONNECTED);
                
                // Start protocol handler
                if (!startProtocolHandler()) {
                    notifyError("Failed to start protocol handler");
                    setState(BridgeState.ERROR);
                    return;
                }
                
                setState(BridgeState.FORWARDING);
                connectionStartTime = System.currentTimeMillis();
                
                Log.i(TAG, "UDP Bridge started successfully");
                
                // Start monitoring loop
                startMonitoring();
                
            } catch (Exception e) {
                Log.e(TAG, "Error starting bridge", e);
                notifyError("Exception starting bridge: " + e.getMessage());
                setState(BridgeState.ERROR);
            }
        });
        
        return true;
    }
    
    /**
     * Stop UDP bridge
     */
    public void stopBridge() {
        Log.i(TAG, "Stopping UDP bridge");
        
        bridgeHandler.post(() -> {
            try {
                nativeStopBridge();
                setState(BridgeState.DISCONNECTED);
                connectionStartTime = 0;
                totalBytesTransferred = 0;
                activeClients = 0;
                
                Log.i(TAG, "UDP Bridge stopped");
                
            } catch (Exception e) {
                Log.e(TAG, "Error stopping bridge", e);
            }
        });
    }
    
    /**
     * Start monitoring bridge statistics
     */
    private void startMonitoring() {
        bridgeHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (currentState == BridgeState.FORWARDING) {
                    updateStatistics();
                    
                    // Schedule next update
                    bridgeHandler.postDelayed(this, 5000); // Every 5 seconds
                }
            }
        }, 5000);
    }
    
    /**
     * Update bridge statistics
     */
    private void updateStatistics() {
        try {
            if (isProtocolConnected()) {
                int newClientCount = getClientCount();
                long newBytesTransferred = getBytesTransferred();
                
                if (newClientCount != activeClients) {
                    activeClients = newClientCount;
                    // Notify client count change if needed
                }
                
                if (newBytesTransferred != totalBytesTransferred) {
                    long delta = newBytesTransferred - totalBytesTransferred;
                    totalBytesTransferred = newBytesTransferred;
                    
                    if (eventListener != null) {
                        eventListener.onDataTransferred(delta);
                    }
                }
            } else {
                // Connection lost
                notifyError("Lost connection to bridge server");
                setState(BridgeState.ERROR);
                
                if (config.isAutoReconnectEnabled()) {
                    Log.i(TAG, "Attempting auto-reconnect...");
                    bridgeHandler.postDelayed(() -> startBridge(), 5000);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error updating statistics", e);
        }
    }
    
    /**
     * Get current bridge state
     */
    public BridgeState getCurrentState() {
        return currentState;
    }
    
    /**
     * Get current configuration
     */
    public UdpBridgeConfig getConfig() {
        return config;
    }
    
    /**
     * Get connection uptime in milliseconds
     */
    public long getUptime() {
        if (connectionStartTime == 0) {
            return 0;
        }
        return System.currentTimeMillis() - connectionStartTime;
    }
    
    /**
     * Get total bytes transferred
     */
    public long getTotalBytesTransferred() {
        return totalBytesTransferred;
    }
    
    /**
     * Get active client count
     */
    public int getActiveClientCount() {
        return activeClients;
    }
    
    /**
     * Get bridge statistics as formatted string
     */
    public String getStatisticsString() {
        StringBuilder stats = new StringBuilder();
        stats.append("State: ").append(currentState.name()).append("\n");
        stats.append("Uptime: ").append(formatUptime(getUptime())).append("\n");
        stats.append("Clients: ").append(activeClients).append("\n");
        stats.append("Transferred: ").append(formatBytes(totalBytesTransferred));
        return stats.toString();
    }
    
    /**
     * Format uptime duration
     */
    private String formatUptime(long uptimeMs) {
        if (uptimeMs == 0) return "Not connected";
        
        long seconds = uptimeMs / 1000;
        long minutes = seconds / 60;
        long hours = minutes / 60;
        
        if (hours > 0) {
            return String.format("%dh %dm %ds", hours, minutes % 60, seconds % 60);
        } else if (minutes > 0) {
            return String.format("%dm %ds", minutes, seconds % 60);
        } else {
            return String.format("%ds", seconds);
        }
    }
    
    /**
     * Format bytes with appropriate units
     */
    private String formatBytes(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return String.format("%.1f KB", bytes / 1024.0);
        if (bytes < 1024 * 1024 * 1024) return String.format("%.1f MB", bytes / (1024.0 * 1024.0));
        return String.format("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    
    /**
     * Set bridge state and notify listeners
     */
    private void setState(BridgeState newState) {
        if (currentState != newState) {
            BridgeState oldState = currentState;
            currentState = newState;
            
            Log.d(TAG, "Bridge state changed: " + oldState + " -> " + newState);
            
            if (eventListener != null) {
                eventListener.onStateChanged(newState);
            }
        }
    }
    
    /**
     * Notify error to listeners
     */
    private void notifyError(String error) {
        Log.e(TAG, "Bridge error: " + error);
        
        if (eventListener != null) {
            eventListener.onError(error);
        }
    }
    
    /**
     * Test bridge connectivity
     */
    public boolean testConnectivity() {
        try {
            return isProtocolConnected();
        } catch (Exception e) {
            Log.e(TAG, "Error testing connectivity", e);
            return false;
        }
    }
    
    /**
     * Integration with SSH Tunnel Service
     */
    public void setSshTunnelService(SshTunnelService sshService) {
        this.sshService = sshService;
    }
    
    /**
     * Check if SSH tunnel is required and available
     */
    public boolean isSshTunnelRequired() {
        // If bridge host is not localhost, we might need SSH tunnel
        String host = config.getBridgeHost();
        return !host.equals("127.0.0.1") && !host.equals("localhost") && 
               !host.startsWith("192.168.") && !host.startsWith("10.");
    }
    
    public boolean isSshTunnelAvailable() {
        return sshService != null && sshService.isConnected();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        stopBridge();
        
        if (bridgeThread != null) {
            bridgeThread.quitSafely();
        }
        
        super.onDestroy();
        Log.d(TAG, "UDP Bridge Service destroyed");
    }
}
