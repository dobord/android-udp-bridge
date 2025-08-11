package com.example.udpbridge;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Configuration class for UDP Bridge settings
 */
public class UdpBridgeConfig {
    private static final String PREFS_NAME = "udp_bridge_prefs";
    private static final String KEY_ENABLED = "bridge_enabled";
    private static final String KEY_BRIDGE_NAME = "bridge_name";
    private static final String KEY_LOCAL_PORT = "local_port";
    private static final String KEY_BRIDGE_HOST = "bridge_host";
    private static final String KEY_BRIDGE_PORT = "bridge_port";
    private static final String KEY_AUTO_RECONNECT = "auto_reconnect";
    private static final String KEY_CONNECTION_TIMEOUT = "connection_timeout";
    
    // Default values
    public static final String DEFAULT_BRIDGE_NAME = "Default Bridge";
    public static final int DEFAULT_LOCAL_PORT = 5060;
    public static final String DEFAULT_BRIDGE_HOST = "127.0.0.1";
    public static final int DEFAULT_BRIDGE_PORT = 8080;
    public static final boolean DEFAULT_AUTO_RECONNECT = true;
    public static final int DEFAULT_CONNECTION_TIMEOUT = 30;
    
    private SharedPreferences prefs;
    
    public UdpBridgeConfig(Context context) {
        prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
    
    // Bridge enabled/disabled
    public boolean isBridgeEnabled() {
        return prefs.getBoolean(KEY_ENABLED, false);
    }
    
    public void setBridgeEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_ENABLED, enabled).apply();
    }
    
    // Bridge name
    public String getBridgeName() {
        return prefs.getString(KEY_BRIDGE_NAME, DEFAULT_BRIDGE_NAME);
    }
    
    public void setBridgeName(String name) {
        prefs.edit().putString(KEY_BRIDGE_NAME, name).apply();
    }
    
    // Local UDP port
    public int getLocalPort() {
        return prefs.getInt(KEY_LOCAL_PORT, DEFAULT_LOCAL_PORT);
    }
    
    public void setLocalPort(int port) {
        prefs.edit().putInt(KEY_LOCAL_PORT, port).apply();
    }
    
    // Bridge server host
    public String getBridgeHost() {
        return prefs.getString(KEY_BRIDGE_HOST, DEFAULT_BRIDGE_HOST);
    }
    
    public void setBridgeHost(String host) {
        prefs.edit().putString(KEY_BRIDGE_HOST, host).apply();
    }
    
    // Bridge server port
    public int getBridgePort() {
        return prefs.getInt(KEY_BRIDGE_PORT, DEFAULT_BRIDGE_PORT);
    }
    
    public void setBridgePort(int port) {
        prefs.edit().putInt(KEY_BRIDGE_PORT, port).apply();
    }
    
    // Auto reconnect
    public boolean isAutoReconnectEnabled() {
        return prefs.getBoolean(KEY_AUTO_RECONNECT, DEFAULT_AUTO_RECONNECT);
    }
    
    public void setAutoReconnectEnabled(boolean enabled) {
        prefs.edit().putBoolean(KEY_AUTO_RECONNECT, enabled).apply();
    }
    
    // Connection timeout
    public int getConnectionTimeout() {
        return prefs.getInt(KEY_CONNECTION_TIMEOUT, DEFAULT_CONNECTION_TIMEOUT);
    }
    
    public void setConnectionTimeout(int timeout) {
        prefs.edit().putInt(KEY_CONNECTION_TIMEOUT, timeout).apply();
    }
    
    /**
     * Reset all settings to defaults
     */
    public void resetToDefaults() {
        SharedPreferences.Editor editor = prefs.edit();
        editor.clear();
        editor.putInt(KEY_LOCAL_PORT, DEFAULT_LOCAL_PORT);
        editor.putString(KEY_BRIDGE_HOST, DEFAULT_BRIDGE_HOST);
        editor.putInt(KEY_BRIDGE_PORT, DEFAULT_BRIDGE_PORT);
        editor.putBoolean(KEY_AUTO_RECONNECT, DEFAULT_AUTO_RECONNECT);
        editor.putInt(KEY_CONNECTION_TIMEOUT, DEFAULT_CONNECTION_TIMEOUT);
        editor.apply();
    }
    
    /**
     * Get configuration summary as string
     */
    public String getConfigSummary() {
        StringBuilder summary = new StringBuilder();
        summary.append("Bridge: ").append(isBridgeEnabled() ? "Enabled" : "Disabled").append("\n");
        summary.append("Local Port: ").append(getLocalPort()).append("\n");
        summary.append("Bridge Server: ").append(getBridgeHost()).append(":").append(getBridgePort()).append("\n");
        summary.append("Auto Reconnect: ").append(isAutoReconnectEnabled() ? "Yes" : "No").append("\n");
        summary.append("Timeout: ").append(getConnectionTimeout()).append("s");
        return summary.toString();
    }
    
    /**
     * Validate configuration
     */
    public boolean isValid() {
        return getLocalPort() > 0 && getLocalPort() < 65536 &&
               getBridgeHost() != null && !getBridgeHost().trim().isEmpty() &&
               getBridgePort() > 0 && getBridgePort() < 65536 &&
               getConnectionTimeout() > 0;
    }
}
