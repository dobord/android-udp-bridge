package com.example.sshtunnel;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;

public class SshTunnelService extends Service {
    private static final String TAG = "SshTunnelService";
    private boolean isConnected = false;
    
    // Load native library
    static {
        System.loadLibrary("ssh_tunnel");
    }
    
    // Native methods
    public native boolean connectToServer(String host, int port, String username, String password);
    public native boolean connectWithKey(String host, int port, String username, String privateKeyPath, String passphrase);
    public native void disconnect();
    public native boolean forwardPort(int localPort, String remoteHost, int remotePort);
    
    public class LocalBinder extends Binder {
        SshTunnelService getService() {
            return SshTunnelService.this;
        }
    }
    
    private final IBinder binder = new LocalBinder();

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "SSH Tunnel Service created");
    }

    public boolean connect(String serverAddress, int serverPort, String username, String password) {
        Log.d(TAG, "Attempting to connect to " + serverAddress + ":" + serverPort);
        
        isConnected = connectToServer(serverAddress, serverPort, username, password);
        
        if (isConnected) {
            Log.d(TAG, "Successfully connected to server");
            // Attempt auto-forward if last known config is available via sticky intent extras (set by Activity)
            tryAutoStartForwarding();
        } else {
            Log.e(TAG, "Failed to connect to server");
        }
        
        return isConnected;
    }

    // Called by Activity right before connect() to provide desired forwarding config
    private volatile Integer pendingLocalPort;
    private volatile String pendingRemoteHost;
    private volatile Integer pendingRemotePort;

    public void setPendingForwarding(Integer localPort, String remoteHost, Integer remotePort) {
        this.pendingLocalPort = localPort;
        this.pendingRemoteHost = remoteHost;
        this.pendingRemotePort = remotePort;
    }

    private void tryAutoStartForwarding() {
        if (pendingLocalPort != null && pendingRemoteHost != null && pendingRemotePort != null) {
            Log.d(TAG, "Auto-starting UDP forwarding after connect: " + pendingLocalPort + " -> " + pendingRemoteHost + ":" + pendingRemotePort);
            forwardPort(pendingLocalPort, pendingRemoteHost, pendingRemotePort);
            // One-shot
            pendingLocalPort = null;
            pendingRemoteHost = null;
            pendingRemotePort = null;
        }
    }

    public boolean connectWithPrivateKey(String serverAddress, int serverPort, String username, String privateKeyPath, String passphrase) {
        Log.d(TAG, "Attempting to connect to " + serverAddress + ":" + serverPort + " using private key");
        
        isConnected = connectWithKey(serverAddress, serverPort, username, privateKeyPath, passphrase);
        
        if (isConnected) {
            Log.d(TAG, "Successfully connected to server with private key");
        } else {
            Log.e(TAG, "Failed to connect to server with private key");
        }
        
        return isConnected;
    }

    public boolean startUdpForwarding(int localPort, String remoteHost, int remotePort) {
        if (!isConnected) {
            Log.e(TAG, "Cannot start forwarding: not connected to SSH server");
            return false;
        }
        
        Log.d(TAG, "Starting UDP forwarding: " + localPort + " -> " + remoteHost + ":" + remotePort);
        return forwardPort(localPort, remoteHost, remotePort);
    }

    public void disconnectFromServer() {
        Log.d(TAG, "Disconnecting from server");
        disconnect();
        isConnected = false;
    }
    
    public boolean isConnected() {
        return isConnected;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        disconnectFromServer();
        super.onDestroy();
        Log.d(TAG, "SSH Tunnel Service destroyed");
    }
}