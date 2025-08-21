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
    // Native API: connect returns native handle (0 on failure)
    public native long connectToServer(String sshHost, int sshPort, String username, String password);

    public native long connectWithKey(String sshHost, int sshPort, String username, String privateKeyPath,
        String passphrase);

    // Disconnect using native handle
    public native void disconnect(long handle);

    // udp2tcp native (8-parameter contract, names match ServerConfig)
    public native int startUdp2Tcp(long handle, String remoteBridgeHost, int remoteBridgePort,
            String localBridgeHost, int localBridgePort,
            String localUdpHost, int localUdpPort,
            String remoteUdpHost, int remoteUdpPort);

    // Advanced variant removed on native side; keep Java shim for compatibility
    public native void stopUdp2Tcp(long handle);

    public native String getUdp2TcpStats(long handle);

    public native boolean isUdp2TcpRunning(long handle);

    public native int nativeTlsSelfTest();

    // nativeSetForwardingHint removed; bridge config is applied in startUdp2Tcp
    // Debug dump of native internal state
    public native String nativeDebugDump(long handle);

    // Stored native handle for this service instance (0 == none)
    private volatile long nativeHandle = 0L;

    // Accessor for activities to fetch the native handle
    public long getNativeHandle() {
        return nativeHandle;
    }

    public class LocalBinder extends Binder {
        SshTunnelService getService() {
            return SshTunnelService.this;
        }
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
        long h = connectToServer(serverAddress, serverPort, username, password);
        isConnected = (h != 0);
        if (isConnected) {
            nativeHandle = h;
            Log.d(TAG, "Successfully connected to server");
            tryAutoStartForwarding();
        } else {
            Log.e(TAG, "Failed to connect to server");
        }
        return isConnected;
    }

    public boolean connectWithPrivateKey(String serverAddress, int serverPort, String username, String privateKeyPath,
            String passphrase) {
        Log.d(TAG, "Attempting to connect to " + serverAddress + ":" + serverPort + " using private key");
        long h = connectWithKey(serverAddress, serverPort, username, privateKeyPath, passphrase);
        isConnected = (h != 0);
        if (isConnected) {
            nativeHandle = h;
            Log.d(TAG, "Successfully connected to server with private key");
            tryAutoStartForwarding();
        } else {
            Log.e(TAG, "Failed to connect to server with private key");
        }
        return isConnected;
    }

    // Called by Activity right before connect() to provide desired forwarding
    // config (8-arg contract)
    // Use semantically clear names and keep local copies of original-style settings
    private volatile String pendingRemoteBridgeHost; // maps to remoteBridgeHost
    private volatile Integer pendingRemoteBridgePort; // maps to remoteBridgePort
    private volatile String pendingLocalBridgeHost; // maps from tcpConnectHost
    private volatile Integer pendingLocalBridgePort; // maps from tcpConnectPort
    private volatile String pendingLocalUdpHost; // maps from listenAddr
    private volatile Integer pendingLocalUdpPort; // maps from listenPort
    private volatile String pendingRemoteUdpHost; // maps from remoteDstIp
    private volatile Integer pendingRemoteUdpPort; // maps from remoteDstPort

    public void setPendingForwarding(String remoteBridgeHost, Integer remoteBridgePort,
            String localBridgeHost, Integer localBridgePort,
            String localUdpHost, Integer localUdpPort,
            String remoteUdpHost, Integer remoteUdpPort) {
        // Map 6-arg API into richer pending fields (parameter names aligned with
        // pending fields)
        this.pendingRemoteBridgeHost = remoteBridgeHost;
        this.pendingRemoteBridgePort = remoteBridgePort;
        this.pendingLocalBridgeHost = localBridgeHost;
        this.pendingLocalBridgePort = localBridgePort;
        this.pendingLocalUdpHost = localUdpHost;
        this.pendingLocalUdpPort = localUdpPort;
        this.pendingRemoteUdpHost = remoteUdpHost;
        this.pendingRemoteUdpPort = remoteUdpPort;
    }

    private void tryAutoStartForwarding() {
        if (pendingRemoteBridgeHost != null && pendingRemoteBridgePort != null &&
                pendingLocalBridgeHost != null && pendingLocalBridgePort != null &&
                pendingLocalUdpHost != null && pendingLocalUdpPort != null &&
                pendingRemoteUdpHost != null && pendingRemoteUdpPort != null) {

            Log.d(TAG, "Auto-starting UDP forwarding after connect: " +
                    "remoteBridgeHost=" + pendingRemoteBridgeHost + " " +
                    "remoteBridgePort=" + pendingRemoteBridgePort + " " +
                    "localUdpHost=" + pendingLocalUdpHost + " " +
                    "localUdpPort=" + pendingLocalUdpPort + " " +
                    "localBridgeHost=" + pendingLocalBridgeHost + " " +
                    "localBridgePort=" + pendingLocalBridgePort + " " +
                    "remoteUdpHost=" + pendingRemoteUdpHost + " " +
                    "remoteUdpPort=" + pendingRemoteUdpPort);

            boolean ok = startUdpForwarding(
                    pendingRemoteBridgeHost, pendingRemoteBridgePort,
                    pendingLocalBridgeHost, pendingLocalBridgePort,
                    pendingLocalUdpHost, pendingLocalUdpPort,
                    pendingRemoteUdpHost, pendingRemoteUdpPort);
            // Clear pending config regardless of success, to avoid repeated attempts
            pendingRemoteBridgeHost = null;
            pendingRemoteBridgePort = null;
            pendingLocalBridgeHost = null;
            pendingLocalBridgePort = null;
            pendingLocalUdpHost = null;
            pendingLocalUdpPort = null;
            pendingRemoteUdpHost = null;
            pendingRemoteUdpPort = null;
        }
    }

    private static final long UDP2TCP_STATS_PERIOD_MS = 1000L;
    private final ScheduledExecutorService udp2tcpStatsExec = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "udp2tcp-stats");
        t.setDaemon(true);
        return t;
    });
    private ScheduledFuture<?> udp2tcpStatsFuture;

    public boolean startUdpForwarding(String remoteBridgeHost, int remoteBridgePort,
            String localBridgeHost, int localBridgePort,
            String localUdpHost, int localUdpPort,
            String remoteUdpHost, int remoteUdpPort) {
        if (!isConnected) {
            Log.e(TAG, "Cannot start forwarding: not connected to SSH server");
            return false;
        }
        Log.d(TAG, "Starting UDP forwarding (udp2tcp): " +
                "remoteBridgeHost=" + remoteBridgeHost + " " +
                "remoteBridgePort=" + remoteBridgePort + " " +
                "localBridgeHost=" + localBridgeHost + " " +
                "localBridgePort=" + localBridgePort + " " +
                "localUdpHost=" + localUdpHost + " " +
                "localUdpPort=" + localUdpPort + " " +
                "remoteUdpHost=" + remoteUdpHost + " " +
                "remoteUdpPort=" + remoteUdpPort);
    int rc = startUdp2Tcp(
        nativeHandle,
        remoteBridgeHost != null ? remoteBridgeHost : "127.0.0.1",
        remoteBridgePort,
        localBridgeHost != null ? localBridgeHost : "127.0.0.1",
        localBridgePort,
        localUdpHost != null ? localUdpHost : "127.0.0.1",
        localUdpPort,
        remoteUdpHost != null ? remoteUdpHost : "127.0.0.1",
        remoteUdpPort);
        if (rc == 0) {
            activeUdpLocalPort = localUdpPort;
            startUdp2TcpStatsPolling();
            return true;
        }
        Log.e(TAG, "Failed to start udp2tcp (rc=" + rc + ")");
        return false;
    }

    // Removed deprecated startUdpForwardingAdvanced

    // Removed deprecated startUdp2TcpAdvanced shim

    private void startUdp2TcpStatsPolling() {
        stopUdp2TcpStatsPolling();
        udp2tcpStatsFuture = udp2tcpStatsExec.scheduleAtFixedRate(() -> {
            try {
                if (!isUdp2TcpRunning(nativeHandle))
                    return;
                String stats = getUdp2TcpStats(nativeHandle);
                Log.d(TAG, "udp2tcp stats: \n" + stats);
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

    // ================= Test UDP traffic helpers =================
    public boolean sendTestUdp(String payload) {
        return sendTestUdpInternal(payload != null ? payload.getBytes(StandardCharsets.UTF_8) : new byte[0]);
    }

    public boolean sendTestUdpRandom(int size) {
        if (size <= 0)
            size = 1;
        if (size > 65507)
            size = 65507; // UDP max payload
        byte[] buf = new byte[size];
        for (int i = 0; i < size; i++)
            buf[i] = (byte) (i & 0xFF);
        return sendTestUdpInternal(buf);
    }

    public boolean sendTestUdpBurst(int packets, int sizeEach) {
        if (packets <= 0)
            packets = 1;
        if (packets > 1000)
            packets = 1000; // safety cap
        boolean okAll = true;
        for (int i = 0; i < packets; i++) {
            boolean ok = sendTestUdpRandom(sizeEach);
            if (!ok) {
                okAll = false;
                break;
            }
        }
        return okAll;
    }

    private boolean sendTestUdpInternal(byte[] data) {
        Integer port = activeUdpLocalPort;
        if (port == null) {
            Log.w(TAG, "sendTestUdp: no active UDP forwarding (port null)");
            return false;
        }
        if (!isUdp2TcpRunning(nativeHandle)) {
            Log.w(TAG, "sendTestUdp: udp2tcp not running");
            return false;
        }
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
        } finally {
            if (ds != null)
                ds.close();
        }
    }
    // ============================================================

    public void disconnectFromServer() {
        Log.d(TAG, "Disconnecting from server");
        try {
            String dbg = nativeDebugDump(nativeHandle);
            if (dbg != null)
                Log.d(TAG, "Pre-disconnect native debug:\n" + dbg);
        } catch (Throwable t) {
            Log.w(TAG, "nativeDebugDump failed (pre)", t);
        }
        if (isUdp2TcpRunning(nativeHandle)) {
            Log.d(TAG, "Stopping udp2tcp before SSH disconnect");
            stopUdp2Tcp(nativeHandle);
        }
        stopUdp2TcpStatsPolling();
        disconnect(nativeHandle);
        isConnected = false;
        activeUdpLocalPort = null;
        try {
            String dbg2 = nativeDebugDump(nativeHandle);
            if (dbg2 != null)
                Log.d(TAG, "Post-disconnect native debug:\n" + dbg2);
        } catch (Throwable t) {
            Log.w(TAG, "nativeDebugDump failed (post)", t);
        }
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