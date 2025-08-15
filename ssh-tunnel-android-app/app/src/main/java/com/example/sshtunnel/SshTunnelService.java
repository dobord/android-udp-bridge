package com.example.sshtunnel;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
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
    // udp2tcp native (6-parameter contract)
    public native int startUdp2Tcp(String tcpConnectHost, int tcpConnectPort,
                                   String listenAddr, int listenPort,
                                   String remoteDstIp, int remoteDstPort);
    // Advanced variant removed on native side; keep Java shim for compatibility
    public native void stopUdp2Tcp();
    public native String getUdp2TcpStats();
    public native boolean isUdp2TcpRunning();
    public native int nativeTlsSelfTest();
    // nativeSetForwardingHint removed; bridge config is applied in startUdp2Tcp
    // Debug dump of native internal state
    public native String nativeDebugDump();

    public class LocalBinder extends Binder {
        SshTunnelService getService() { return SshTunnelService.this; }
    }

    private final IBinder binder = new LocalBinder();

    // Store last requested UDP forwarding params for test traffic
    private volatile Integer activeUdpLocalPort; // local UDP port listened by udp2tcp

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
            tryAutoStartForwarding();
        } else {
            Log.e(TAG, "Failed to connect to server");
        }
        return isConnected;
    }

    // Called by Activity right before connect() to provide desired forwarding config (6-arg contract)
    private volatile String pendingTcpConnectHost;
    private volatile Integer pendingTcpConnectPort;
    private volatile String pendingListenAddr;
    private volatile Integer pendingListenPort;
    private volatile String pendingRemoteDstIp;
    private volatile Integer pendingRemoteDstPort;

    public void setPendingForwarding(String tcpConnectHost, Integer tcpConnectPort,
                                     String listenAddr, Integer listenPort,
                                     String remoteDstIp, Integer remoteDstPort) {
        this.pendingTcpConnectHost = tcpConnectHost;
        this.pendingTcpConnectPort = tcpConnectPort;
        this.pendingListenAddr = listenAddr;
        this.pendingListenPort = listenPort;
        this.pendingRemoteDstIp = remoteDstIp;
        this.pendingRemoteDstPort = remoteDstPort;
        // Bridge hint no longer needed; startUdp2Tcp will apply config
    }

    private void tryAutoStartForwarding() {
        if (pendingTcpConnectHost != null && pendingTcpConnectPort != null &&
            pendingListenAddr != null && pendingListenPort != null &&
            pendingRemoteDstIp != null && pendingRemoteDstPort != null) {
            Log.d(TAG, "Auto-starting UDP forwarding after connect: listen=" + pendingListenAddr + ":" + pendingListenPort +
                    " tcpConnect=" + pendingTcpConnectHost + ":" + pendingTcpConnectPort +
                    " dstUdp=" + pendingRemoteDstIp + ":" + pendingRemoteDstPort);
            boolean ok = startUdpForwarding(
                pendingTcpConnectHost, pendingTcpConnectPort,
                pendingListenAddr, pendingListenPort,
                pendingRemoteDstIp, pendingRemoteDstPort
            );
            // Clear pending config regardless of success, to avoid repeated attempts
            pendingTcpConnectHost = null;
            pendingTcpConnectPort = null;
            pendingListenAddr = null;
            pendingListenPort = null;
            pendingRemoteDstIp = null;
            pendingRemoteDstPort = null;
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
        Thread t = new Thread(r, "udp2tcp-stats"); t.setDaemon(true); return t; });
    private ScheduledFuture<?> udp2tcpStatsFuture;

    public boolean startUdpForwarding(String tcpConnectHost, int tcpConnectPort,
                      String listenAddr, int listenPort,
                      String remoteDstIp, int remoteDstPort) {
    if (!isConnected) { Log.e(TAG, "Cannot start forwarding: not connected to SSH server"); return false; }
    Log.d(TAG, "Starting UDP forwarding (udp2tcp) with 6-arg API: tcpConnect=" + tcpConnectHost + ":" + tcpConnectPort +
        " listen=" + listenAddr + ":" + listenPort + " dstUdp=" + remoteDstIp + ":" + remoteDstPort);
    int rc = startUdp2Tcp(
        tcpConnectHost != null ? tcpConnectHost : "127.0.0.1",
        tcpConnectPort,
        listenAddr != null ? listenAddr : "0.0.0.0",
        listenPort,
        remoteDstIp != null ? remoteDstIp : "127.0.0.1",
        remoteDstPort
    );
    if (rc == 0) { activeUdpLocalPort = listenPort; startUdp2TcpStatsPolling(); return true; }
    Log.e(TAG, "Failed to start udp2tcp (rc=" + rc + ")");
    return false;
    }

    // Removed deprecated startUdpForwardingAdvanced

    // Removed deprecated startUdp2TcpAdvanced shim

    private void startUdp2TcpStatsPolling() {
        stopUdp2TcpStatsPolling();
        udp2tcpStatsFuture = udp2tcpStatsExec.scheduleAtFixedRate(() -> {
            try {
                if (!isUdp2TcpRunning()) return;
                String stats = getUdp2TcpStats();
                Log.d(TAG, "udp2tcp stats: \n" + stats);
            } catch (Throwable t) { Log.w(TAG, "Stats polling error", t); }
        }, 0, UDP2TCP_STATS_PERIOD_MS, TimeUnit.MILLISECONDS);
    }

    private void stopUdp2TcpStatsPolling() {
        if (udp2tcpStatsFuture != null) { udp2tcpStatsFuture.cancel(true); udp2tcpStatsFuture = null; }
    }

    // ================= Test UDP traffic helpers =================
    public boolean sendTestUdp(String payload) { return sendTestUdpInternal(payload != null ? payload.getBytes(StandardCharsets.UTF_8) : new byte[0]); }

    public boolean sendTestUdpRandom(int size) {
        if (size <= 0) size = 1; if (size > 65507) size = 65507; // UDP max payload
        byte[] buf = new byte[size];
        for (int i = 0; i < size; i++) buf[i] = (byte)(i & 0xFF);
        return sendTestUdpInternal(buf);
    }

    public boolean sendTestUdpBurst(int packets, int sizeEach) {
        if (packets <= 0) packets = 1; if (packets > 1000) packets = 1000; // safety cap
        boolean okAll = true;
        for (int i = 0; i < packets; i++) {
            boolean ok = sendTestUdpRandom(sizeEach);
            if (!ok) { okAll = false; break; }
        }
        return okAll;
    }

    private boolean sendTestUdpInternal(byte[] data) {
        Integer port = activeUdpLocalPort;
        if (port == null) { Log.w(TAG, "sendTestUdp: no active UDP forwarding (port null)"); return false; }
        if (!isUdp2TcpRunning()) { Log.w(TAG, "sendTestUdp: udp2tcp not running"); return false; }
        DatagramSocket ds = null;
        try {
            ds = new DatagramSocket();
            ds.setSoTimeout(1000);
            InetAddress addr = InetAddress.getByName("127.0.0.1");
            DatagramPacket pkt = new DatagramPacket(data, data.length, addr, port);
            ds.send(pkt);
            Log.d(TAG, "sendTestUdp: sent " + data.length + " bytes to 127.0.0.1:" + port);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "sendTestUdp failed", e);
            return false;
        } finally { if (ds != null) ds.close(); }
    }
    // ============================================================

    public void disconnectFromServer() {
        Log.d(TAG, "Disconnecting from server");
        try { String dbg = nativeDebugDump(); if (dbg != null) Log.d(TAG, "Pre-disconnect native debug:\n" + dbg); } catch (Throwable t) { Log.w(TAG, "nativeDebugDump failed (pre)", t); }
        if (isUdp2TcpRunning()) { Log.d(TAG, "Stopping udp2tcp before SSH disconnect"); stopUdp2Tcp(); }
        stopUdp2TcpStatsPolling();
        disconnect();
        isConnected = false;
        activeUdpLocalPort = null;
        try { String dbg2 = nativeDebugDump(); if (dbg2 != null) Log.d(TAG, "Post-disconnect native debug:\n" + dbg2); } catch (Throwable t) { Log.w(TAG, "nativeDebugDump failed (post)", t); }
    }

    public boolean isConnected() { return isConnected; }

    @Override public IBinder onBind(Intent intent) { return binder; }

    @Override public void onDestroy() {
        disconnectFromServer();
        udp2tcpStatsExec.shutdownNow();
        super.onDestroy();
        Log.d(TAG, "SSH Tunnel Service destroyed");
    }
}