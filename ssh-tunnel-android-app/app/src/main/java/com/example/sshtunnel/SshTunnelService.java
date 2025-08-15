package com.example.sshtunnel;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

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
    // udp2tcp native
    public native int startUdp2Tcp(String remoteHost, int remotePort, int localUdpPort);
    public native int startUdp2TcpAdvanced(String remoteHost, int remotePort, int localUdpPort, String dstIp, int dstPort);
    public native void stopUdp2Tcp();
    public native String getUdp2TcpStats();
    public native boolean isUdp2TcpRunning();
    public native int nativeTlsSelfTest();

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
            int rc = startUdp2Tcp(pendingRemoteHost, pendingRemotePort, pendingLocalPort);
            if (rc == 0) startUdp2TcpStatsPolling();
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

    private static final long UDP2TCP_STATS_PERIOD_MS = 1000L;
    private final ScheduledExecutorService udp2tcpStatsExec = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "udp2tcp-stats");
        t.setDaemon(true);
        return t;
    });
    private ScheduledFuture<?> udp2tcpStatsFuture;

    public boolean startUdpForwarding(int localPort, String remoteHost, int remotePort) {
        if (!isConnected) {
            Log.e(TAG, "Cannot start forwarding: not connected to SSH server");
            return false;
        }

        Log.d(TAG, "Starting UDP forwarding (udp2tcp): " + localPort + " -> " + remoteHost + ":" + remotePort);
        int rc = startUdp2Tcp(remoteHost, remotePort, localPort);
        if (rc == 0) {
            startUdp2TcpStatsPolling();
            return true;
        }
        Log.e(TAG, "Failed to start udp2tcp (rc=" + rc + ")");
        return false;
    }

    // Advanced: позволяет задать удалённый UDP dst endpoint (dstIp:dstPort)
    public boolean startUdpForwardingAdvanced(int localPort, String remoteHost, int remotePort, String dstIp, int dstPort) {
        if (!isConnected) {
            Log.e(TAG, "Cannot start advanced forwarding: not connected to SSH server");
            return false;
        }
        Log.d(TAG, "Starting advanced UDP forwarding: local=" + localPort + " remoteTcp=" + remoteHost + ":" + remotePort +
                " dstUdp=" + dstIp + ":" + dstPort);
        int rc = startUdp2TcpAdvanced(remoteHost, remotePort, localPort, dstIp, dstPort);
        if (rc == 0) {
            startUdp2TcpStatsPolling();
            return true;
        }
        Log.e(TAG, "Failed to start advanced udp2tcp (rc=" + rc + ")");
        return false;
    }

    private void startUdp2TcpStatsPolling() {
        stopUdp2TcpStatsPolling();
        udp2tcpStatsFuture = udp2tcpStatsExec.scheduleAtFixedRate(() -> {
            try {
                if (!isUdp2TcpRunning()) return;
                String stats = getUdp2TcpStats();
                Log.d(TAG, "udp2tcp stats: \n" + stats);
                // TODO: broadcast or callback to UI if needed
            } catch (Throwable t) {
                Log.w(TAG, "Stats polling error", t);
            }
        }, 0, UDP2TCP_STATS_PERIOD_MS, TimeUnit.MILLISECONDS);
    }

    private void stopUdp2TcpStatsPolling() {
        if (udp2tcpStatsFuture != null) {
            udp2tcpStatsFuture.cancel(true);
            udp2tcpStatsFuture = null;
        }
    }

    public void disconnectFromServer() {
        Log.d(TAG, "Disconnecting from server");
        if (isUdp2TcpRunning()) {
            Log.d(TAG, "Stopping udp2tcp before SSH disconnect");
            stopUdp2Tcp();
        }
        stopUdp2TcpStatsPolling();
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
        udp2tcpStatsExec.shutdownNow();
        super.onDestroy();
        Log.d(TAG, "SSH Tunnel Service destroyed");
    }
}