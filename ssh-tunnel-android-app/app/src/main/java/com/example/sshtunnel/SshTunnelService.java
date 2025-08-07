package com.example.sshtunnel;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Binder;
import android.os.Build;
import android.util.Log;
import androidx.core.app.NotificationCompat;

public class SshTunnelService extends Service {
    private static final String TAG = "SshTunnelService";
    private static final String CHANNEL_ID = "SSH_TUNNEL_CHANNEL";
    private static final int NOTIFICATION_ID = 1;
    
    private boolean isConnected = false;
    private boolean isTunnelActive = false;
    private String currentTunnelInfo = "";
    
    // Load native library
    static {
        System.loadLibrary("ssh_tunnel");
    }
    
    // Native methods
    public native boolean connectToServer(String host, int port, String username, String password);
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
        createNotificationChannel();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "SSH Tunnel Service",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Manages SSH tunnel connections");
            
            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(channel);
        }
    }

    private void startForegroundNotification(String message) {
        Intent notificationIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
            this, 0, notificationIntent, 
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? 
                PendingIntent.FLAG_IMMUTABLE : 0
        );

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("SSH Tunnel Active")
            .setContentText(message)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW);

        startForeground(NOTIFICATION_ID, builder.build());
    }

    private void updateNotification(String message) {
        if (isConnected || isTunnelActive) {
            startForegroundNotification(message);
        }
    }

    public boolean connect(String serverAddress, int serverPort, String username, String password) {
        Log.d(TAG, "Attempting to connect to " + serverAddress + ":" + serverPort);
        
        try {
            isConnected = connectToServer(serverAddress, serverPort, username, password);
            
            if (isConnected) {
                Log.d(TAG, "Successfully connected to server");
                startForegroundNotification("Connected to " + serverAddress + ":" + serverPort);
            } else {
                Log.e(TAG, "Failed to connect to server");
                stopForeground(true);
            }
        } catch (Exception e) {
            Log.e(TAG, "Exception during connection: " + e.getMessage());
            isConnected = false;
        }
        
        return isConnected;
    }

    public boolean startUdpForwarding(int localPort, String remoteHost, int remotePort) {
        if (!isConnected) {
            Log.e(TAG, "Cannot start forwarding: not connected to SSH server");
            return false;
        }
        
        Log.d(TAG, "Starting port forwarding: " + localPort + " -> " + remoteHost + ":" + remotePort);
        
        try {
            boolean success = forwardPort(localPort, remoteHost, remotePort);
            
            if (success) {
                isTunnelActive = true;
                currentTunnelInfo = localPort + " -> " + remoteHost + ":" + remotePort;
                updateNotification("Forwarding " + currentTunnelInfo);
                Log.i(TAG, "Port forwarding started successfully");
            } else {
                Log.e(TAG, "Failed to start port forwarding");
            }
            
            return success;
        } catch (Exception e) {
            Log.e(TAG, "Exception during port forwarding: " + e.getMessage());
            return false;
        }
    }

    public void disconnectFromServer() {
        Log.d(TAG, "Disconnecting from server");
        
        try {
            disconnect();
        } catch (Exception e) {
            Log.e(TAG, "Exception during disconnect: " + e.getMessage());
        } finally {
            isConnected = false;
            isTunnelActive = false;
            currentTunnelInfo = "";
            stopForeground(true);
        }
    }
    
    public boolean isConnected() {
        return isConnected;
    }
    
    public boolean isTunnelActive() {
        return isTunnelActive;
    }
    
    public String getCurrentTunnelInfo() {
        return currentTunnelInfo;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        Log.d(TAG, "SSH Tunnel Service being destroyed");
        disconnectFromServer();
        super.onDestroy();
    }
}