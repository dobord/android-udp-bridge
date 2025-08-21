package com.example.sshtunnel;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioGroup;
import android.widget.RadioButton;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.switchmaterial.SwitchMaterial;
import com.google.android.material.textfield.TextInputLayout;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.floatingactionbutton.FloatingActionButton;

import com.example.sshtunnel.models.ServerConfig;
import com.example.sshtunnel.models.ServerConfigManager;

import java.util.List;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_CODE_SERVER_CONFIG = 1001;

    private SshTunnelService sshTunnelService;
    private boolean serviceBound = false;

    // UI update handler
    private Handler uiUpdateHandler = new Handler();
    private Runnable uiUpdateRunnable;

    // Server management
    private ServerConfigManager serverConfigManager;
    private ServerConfig currentServerConfig;

    // UI Elements - New simplified UI
    private MaterialButton connectButton;
    private TextView bridgeStatsTextView;
    private TextView networkStatsTextView;
    private AutoCompleteTextView serverDropdown;
    private FloatingActionButton addServerButton;
    private MaterialButton settingsButton;

    private ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            SshTunnelService.LocalBinder binder = (SshTunnelService.LocalBinder) service;
            sshTunnelService = binder.getService();
            serviceBound = true;
            updateUI();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            serviceBound = false;
        }
    };

    // Removed legacy UdpBridgeService: stats now come from SshTunnelService udp2tcp
    // polling

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Initialize server config manager
        serverConfigManager = new ServerConfigManager(this);

        initializeViews();
        setupClickListeners();
        loadLastSelectedServer();

        // Bind to SSH service
        Intent intent = new Intent(this, SshTunnelService.class);
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);

        // Start periodic UI updates
        startPeriodicUIUpdates();
    }

    private void initializeViews() {
        // New simplified UI
        connectButton = findViewById(R.id.connect_button);
        bridgeStatsTextView = findViewById(R.id.bridge_stats_text_view);
    networkStatsTextView = findViewById(R.id.network_stats_text_view);
        serverDropdown = findViewById(R.id.server_dropdown);
        addServerButton = findViewById(R.id.add_server_button);
        settingsButton = findViewById(R.id.settings_button);

        // Setup server dropdown
        updateServerDropdown();
    }

    private void updateServerDropdown() {
        List<ServerConfig> configs = serverConfigManager.getAllConfigs();
        String[] serverNames = new String[configs.size()];

        for (int i = 0; i < configs.size(); i++) {
            serverNames[i] = configs.get(i).getDisplayName();
        }

        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                R.layout.dropdown_item_white_text, serverNames);
        serverDropdown.setAdapter(adapter);

        // Determine which server to select
        ServerConfig serverToSelect = null;

        // First priority: current server if it still exists
        if (currentServerConfig != null) {
            ServerConfig currentFromManager = serverConfigManager.getConfigById(currentServerConfig.getId());
            if (currentFromManager != null) {
                serverToSelect = currentFromManager;
                currentServerConfig = currentFromManager; // Update reference
            }
        }

        // Second priority: last selected server
        if (serverToSelect == null) {
            serverToSelect = serverConfigManager.getLastSelectedConfig();
        }

        // Third priority: first available server
        if (serverToSelect == null && configs.size() > 0) {
            serverToSelect = configs.get(0);
            serverConfigManager.setLastSelectedConfigId(serverToSelect.getId());
        }

        // Update UI
        if (serverToSelect != null) {
            serverDropdown.setText(serverToSelect.getDisplayName(), false);
            currentServerConfig = serverToSelect;
        } else {
            // No servers configured
            serverDropdown.setText("", false);
            currentServerConfig = null;
        }
    }

    private void loadLastSelectedServer() {
        ServerConfig lastSelected = serverConfigManager.getLastSelectedConfig();
        if (lastSelected != null) {
            serverDropdown.setText(lastSelected.getDisplayName(), false);
            currentServerConfig = lastSelected;
        }
    }

    private void setupClickListeners() {
        connectButton.setOnClickListener(v -> {
            if (serviceBound && sshTunnelService.isConnected()) {
                disconnectFromSshServer();
            } else {
                connectToSshServer();
            }
        });

        // Long-press on status text to send a test UDP burst (manual load simulation)
        bridgeStatsTextView.setOnLongClickListener(v -> {
            if (!serviceBound || sshTunnelService == null) return true;
            new Thread(() -> {
                boolean running = false;
                boolean ok = false;
                try {
                    running = sshTunnelService.isUdp2TcpRunning(sshTunnelService.getNativeHandle());
                    if (running) ok = sshTunnelService.sendTestUdpBurst(10, 128);
                } catch (Throwable t) {
                    ok = false;
                }
                final boolean fRunning = running;
                final boolean fOk = ok;
                runOnUiThread(() -> {
                    if (!fRunning) {
                        Toast.makeText(MainActivity.this, "UDP forwarding is not running", Toast.LENGTH_SHORT).show();
                    } else {
                        Toast.makeText(MainActivity.this, fOk ? "Test UDP burst sent" : "Test UDP burst failed", Toast.LENGTH_SHORT).show();
                    }
                });
            }).start();
            return true;
        });

        serverDropdown.setOnItemClickListener((parent, view, position, id) -> {
            List<ServerConfig> configs = serverConfigManager.getAllConfigs();
            if (position < configs.size()) {
                currentServerConfig = configs.get(position);
                serverConfigManager.setLastSelectedConfigId(currentServerConfig.getId());
                updateUI();
            }
        });

        // Enable dropdown click when no input focus
        serverDropdown.setOnClickListener(v -> {
            if (!serverDropdown.isPopupShowing()) {
                serverDropdown.showDropDown();
            }
        });

        addServerButton.setOnClickListener(v -> {
            Intent intent = new Intent(this, ServerConfigActivity.class);
            intent.putExtra(ServerConfigActivity.EXTRA_IS_EDIT_MODE, false);
            startActivityForResult(intent, REQUEST_CODE_SERVER_CONFIG);
        });

        settingsButton.setOnClickListener(v -> {
            if (currentServerConfig != null) {
                android.util.Log.d("MainActivity", "Opening settings for server ID: " + currentServerConfig.getId());
                Intent intent = new Intent(this, ServerConfigActivity.class);
                intent.putExtra(ServerConfigActivity.EXTRA_SERVER_CONFIG, currentServerConfig);
                intent.putExtra(ServerConfigActivity.EXTRA_IS_EDIT_MODE, true);
                startActivityForResult(intent, REQUEST_CODE_SERVER_CONFIG);
            } else {
                Toast.makeText(this, "Please select a server first", Toast.LENGTH_SHORT).show();
            }
        });

        // Hidden compatibility listeners
    }

    private void connectToSshServer() {
        if (!serviceBound) {
            Toast.makeText(this, "Service not available", Toast.LENGTH_SHORT).show();
            return;
        }

        if (currentServerConfig == null) {
            Toast.makeText(this, "Please select a server first", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!currentServerConfig.isValidSshConfig()) {
            Toast.makeText(this, "Invalid server configuration", Toast.LENGTH_SHORT).show();
            return;
        }

        String remoteBridgeHost = currentServerConfig.getRemoteBridgeHost();
        int remoteBridgePort = currentServerConfig.getRemoteBridgePort();
        int localBridgePort = currentServerConfig.getLocalBridgePort();
        String localBridgeHost = currentServerConfig.getLocalBridgeHost();
        boolean autoReconnect = currentServerConfig.isAutoReconnect();
        int connectionTimeout = currentServerConfig.getConnectionTimeout();

        int localUdpPort = currentServerConfig.getLocalUdpPort();
        String localUdpHost = currentServerConfig.getLocalUdpHost();
        String remoteUdpHost = currentServerConfig.getRemoteUdpHost();
        int remoteUdpPort = currentServerConfig.getRemoteUdpPort();

        // Optional wrappers for conditional forwarding startup
        Integer localUdpPortOpt = null;
        Integer remoteUdpPortOpt = null;
        if (localUdpPort > 0 && remoteUdpPort > 0) {
            localUdpPortOpt = localUdpPort;
            remoteUdpPortOpt = remoteUdpPort;
        }

        // Provide pending forwarding to service (best-effort, 6-arg contract)
        // Map: tcpConnectHost/tcpConnectPort = 127.0.0.1/localBridgePort (SSH local
        // forward),
        // listenAddr/listenPort = 127.0.0.1/localPort (local UDP listener),
        // remoteDstIp/remoteDstPort = remoteUdpHost/remoteUdpPort (remote UDP endpoint)
        if (localUdpPortOpt != null && remoteUdpPortOpt != null && remoteUdpHost != null && !remoteUdpHost.isEmpty()) {
            sshTunnelService.setPendingForwarding(
                    (remoteBridgeHost != null && !remoteBridgeHost.isEmpty()) ? remoteBridgeHost : "127.0.0.1",
                    remoteBridgePort,
                    (localBridgeHost != null && !localBridgeHost.isEmpty()) ? localBridgeHost : "127.0.0.1",
                    localBridgePort,
                    (localUdpHost != null && !localUdpHost.isEmpty()) ? localUdpHost : "127.0.0.1", localUdpPortOpt,
                    remoteUdpHost, remoteUdpPortOpt);
        }

        // Final copies for inner classes
        final String fSshHost = currentServerConfig.getSshHost();
        final int fSshPort = currentServerConfig.getSshPort();
        final String fUsername = currentServerConfig.getUsername();
        final boolean fUsePrivateKey = currentServerConfig.isUsePrivateKey();
        final String fPrivateKeyPath = currentServerConfig.getPrivateKeyPath();
        final String fPassphrase = currentServerConfig.getPassphrase();
        final String fPassword = currentServerConfig.getPassword();
        final Integer fLocalUdpPort = localUdpPortOpt;
        final Integer fRemoteUdpPort = remoteUdpPortOpt;
        final String fRemoteUdpHost = remoteUdpHost;

        new Thread(() -> {
            boolean connected;

            if (fUsePrivateKey) {
                connected = sshTunnelService.connectWithPrivateKey(fSshHost, fSshPort, fUsername, fPrivateKeyPath,
                        fPassphrase);
            } else {
                connected = sshTunnelService.connect(fSshHost, fSshPort, fUsername, fPassword);
            }

            runOnUiThread(() -> {
                if (connected) {
                    String authMethod = fUsePrivateKey ? "private key" : "password";
                    String status = "Connected (" + authMethod + ")";
                    // If forwarding config present, update status meaningfully
                    if (fLocalUdpPort != null && fRemoteUdpPort != null && fRemoteUdpHost != null
                            && !fRemoteUdpHost.isEmpty()) {
                        String displayLocal = (localUdpHost != null && !localUdpHost.isEmpty()) ? localUdpHost
                                : "127.0.0.1";
                        status = "Forwarding " + displayLocal + ":" + fLocalUdpPort + " -> " + fRemoteUdpHost + ":"
                                + fRemoteUdpPort + " ("
                                + authMethod + ")";
                    }
                    Toast.makeText(MainActivity.this, "Connected to SSH server using " + authMethod, Toast.LENGTH_SHORT)
                            .show();
                    bridgeStatsTextView.setText(status);
                } else {
                    Toast.makeText(MainActivity.this, "Failed to connect", Toast.LENGTH_SHORT).show();
                    bridgeStatsTextView.setText("Connection failed");
                }
                updateUI();
            });
        }).start();
    }

    private void disconnectFromSshServer() {
        if (serviceBound) {
            // Stop UDP Bridge first
            stopUdpBridge();

            sshTunnelService.disconnectFromServer();
            bridgeStatsTextView.setText("Disconnected");
            updateUI();
            Toast.makeText(this, "Disconnected from SSH server", Toast.LENGTH_SHORT).show();
        }
    }

    private void startUdpForwarding() {
        if (!serviceBound || !sshTunnelService.isConnected()) {
            Toast.makeText(this, "Not connected to SSH server", Toast.LENGTH_SHORT).show();
            return;
        }

        if (currentServerConfig == null) {
            Toast.makeText(this, "No server configuration selected", Toast.LENGTH_SHORT).show();
            return;
        }

        if (currentServerConfig.getLocalUdpPort() <= 0 || currentServerConfig.getRemoteUdpPort() <= 0) {
            Toast.makeText(this, "UDP forwarding not configured for this server", Toast.LENGTH_SHORT).show();
            return;
        }

        final String fRemoteBridgeHost = currentServerConfig.getRemoteBridgeHost();
        final int fRemoteBridgePort = currentServerConfig.getRemoteBridgePort();
        final int fLocalBridgePort = currentServerConfig.getLocalBridgePort();
        final String fLocalBridgeHost = currentServerConfig.getLocalBridgeHost();
        final int fLocalUdpPort = currentServerConfig.getLocalUdpPort();
        final String fLocalUdpHost = currentServerConfig.getLocalUdpHost();
        final String fRemoteUdpHost = currentServerConfig.getRemoteUdpHost();
        final int fRemoteUdpPort = currentServerConfig.getRemoteUdpPort();

        new Thread(() -> {
            // Start udp2tcp with 8-arg API: remoteBridgeHost/Port, localBridgeHost/Port,
            // localUdpHost/Port, remoteUdpHost/Port
            boolean success = sshTunnelService.startUdpForwarding(
                    (fRemoteBridgeHost != null && !fRemoteBridgeHost.isEmpty()) ? fRemoteBridgeHost : "127.0.0.1",
                    fRemoteBridgePort,
                    (fLocalBridgeHost != null && !fLocalBridgeHost.isEmpty()) ? fLocalBridgeHost : "127.0.0.1",
                    fLocalBridgePort,
                    (fLocalUdpHost != null && !fLocalUdpHost.isEmpty()) ? fLocalUdpHost : "127.0.0.1",
                    fLocalUdpPort,
                    fRemoteUdpHost,
                    fRemoteUdpPort);
            final boolean finalSuccess = success;
            runOnUiThread(() -> {
                if (finalSuccess) {
                    Toast.makeText(MainActivity.this, "UDP forwarding started", Toast.LENGTH_SHORT).show();
                    String displayLocal = (fLocalUdpHost != null && !fLocalUdpHost.isEmpty()) ? fLocalUdpHost
                            : "127.0.0.1";
                    bridgeStatsTextView
                            .setText("Forwarding " + displayLocal + ":" + fLocalUdpPort + " -> " + fRemoteUdpHost + ":"
                                    + fRemoteUdpPort);

                    // Fire a small test packet to simulate client activity and validate path
                    new Thread(() -> {
                        try {
                            boolean ok = sshTunnelService.sendTestUdpRandom(64);
                            android.util.Log.d("MainActivity", "Test UDP random send result: " + ok);
                        } catch (Throwable t) {
                            android.util.Log.w("MainActivity", "Test UDP send error", t);
                        }
                    }).start();
                } else {
                    Toast.makeText(MainActivity.this, "Failed to start UDP forwarding", Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    // UDP Bridge methods

    private void startUdpBridge() {
        Toast.makeText(this, "Legacy bridge removed; SSH udp2tcp starts automatically.", Toast.LENGTH_SHORT).show();
    }

    private void stopUdpBridge() {
        // No-op after migration
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQUEST_CODE_SERVER_CONFIG && resultCode == RESULT_OK) {
            // Check if this is a delete operation
            long deleteServerId = data.getLongExtra("DELETE_SERVER_ID", -1);
            if (deleteServerId != -1) {
                serverConfigManager.deleteConfig(deleteServerId);
                Toast.makeText(this, "Server deleted", Toast.LENGTH_SHORT).show();

                // Clear current config if it was deleted
                if (currentServerConfig != null && currentServerConfig.getId() == deleteServerId) {
                    currentServerConfig = null;
                }

                // Update UI
                updateServerDropdown();
                updateUI();
                return;
            }

            ServerConfig serverConfig = data.getParcelableExtra(ServerConfigActivity.EXTRA_SERVER_CONFIG);
            if (serverConfig != null) {
                boolean isEditMode = data.getBooleanExtra(ServerConfigActivity.EXTRA_IS_EDIT_MODE, false);

                // Debug: Log the received config
                android.util.Log.d("MainActivity",
                        "Received server config with ID: " + serverConfig.getId() + ", isEditMode: " + isEditMode);

                if (isEditMode) {
                    serverConfigManager.updateConfig(serverConfig);
                    Toast.makeText(this, "Server updated", Toast.LENGTH_SHORT).show();

                    // Update current config reference if it's the same server
                    if (currentServerConfig != null && currentServerConfig.getId() == serverConfig.getId()) {
                        currentServerConfig = serverConfig;
                    }
                } else {
                    serverConfigManager.addConfig(serverConfig);
                    Toast.makeText(this, "Server added", Toast.LENGTH_SHORT).show();

                    // Set as current and last selected only for new servers
                    currentServerConfig = serverConfig;
                    serverConfigManager.setLastSelectedConfigId(serverConfig.getId());
                }

                // Update UI
                updateServerDropdown();
                updateUI();
            }
        }
    }

    @Override
    protected void onDestroy() {
        // Stop periodic updates
        if (uiUpdateRunnable != null) {
            uiUpdateHandler.removeCallbacks(uiUpdateRunnable);
        }

        if (serviceBound) {
            unbindService(serviceConnection);
        }
        super.onDestroy();
    }

    private void updateBridgeUI() {
        updateBridgeStats();
    }

    private void updateBridgeStats() {
        // Show live udp2tcp stats under the main button
    if (serviceBound && sshTunnelService != null && sshTunnelService.isUdp2TcpRunning(sshTunnelService.getNativeHandle())) {
            try {
                String stats = sshTunnelService.getUdp2TcpStats(sshTunnelService.getNativeHandle());
                if (stats != null && !stats.isEmpty()) {
                    networkStatsTextView.setText(stats);
                } else {
                    networkStatsTextView.setText("");
                }
            } catch (Throwable t) {
                networkStatsTextView.setText("");
            }
        } else {
            networkStatsTextView.setText("");
        }
    }

    /**
     * Update all UI elements based on current service states
     */
    private void updateUI() {
        // Update connection button text and status based on connection state
        if (serviceBound && sshTunnelService != null) {
            if (sshTunnelService.isConnected()) {
                connectButton.setText("Connected");
                connectButton.setBackgroundTintList(ContextCompat.getColorStateList(this, R.color.vibrant_red));
                if (bridgeStatsTextView.getText().toString().equals("Ready to connect")) {
                    bridgeStatsTextView.setText("Tunnel Established");
                }
            } else {
                connectButton.setText("Connect");
                connectButton.setBackgroundTintList(ContextCompat.getColorStateList(this, R.color.golden_apricot));
                bridgeStatsTextView.setText("Ready to connect");
            }
            connectButton.setEnabled(true);
        } else {
            connectButton.setText("Connect");
            connectButton.setEnabled(false);
            bridgeStatsTextView.setText("Service unavailable");
        }

        // Update server dropdown enabled state
        serverDropdown.setEnabled(currentServerConfig != null || serverConfigManager.hasConfigs());

        // Update settings button
        settingsButton.setEnabled(currentServerConfig != null);

        // Update UDP Bridge UI
        updateBridgeUI();
    }

    private void startPeriodicUIUpdates() {
        uiUpdateRunnable = new Runnable() {
            @Override
            public void run() {
                updateBridgeStats();
                // Schedule next update in 2 seconds
                uiUpdateHandler.postDelayed(this, 2000);
            }
        };
        // Start first update after 1 second
        uiUpdateHandler.postDelayed(uiUpdateRunnable, 1000);
    }
}