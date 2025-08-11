package com.example.sshtunnel;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
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

import com.example.udpbridge.UdpBridgeConfig;
import com.example.udpbridge.UdpBridgeService;
import com.example.sshtunnel.models.ServerConfig;
import com.example.sshtunnel.models.ServerConfigManager;

import java.util.List;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_CODE_SERVER_CONFIG = 1001;

    private SshTunnelService sshTunnelService;
    private UdpBridgeService udpBridgeService;
    private boolean serviceBound = false;
    private boolean bridgeServiceBound = false;
    
    // Server management
    private ServerConfigManager serverConfigManager;
    private ServerConfig currentServerConfig;
    
    // UI Elements - New simplified UI
    private MaterialButton connectButton;
    private TextView bridgeStatsTextView;
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
    
    private ServiceConnection bridgeServiceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            UdpBridgeService.LocalBinder binder = (UdpBridgeService.LocalBinder) service;
            udpBridgeService = binder.getService();
            bridgeServiceBound = true;
            
            // Set up bridge event listener
            udpBridgeService.setEventListener(bridgeEventListener);
            udpBridgeService.setSshTunnelService(sshTunnelService);
            
            updateBridgeUI();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            bridgeServiceBound = false;
        }
    };
    
    // Bridge event listener
    private UdpBridgeService.BridgeEventListener bridgeEventListener = new UdpBridgeService.BridgeEventListener() {
        @Override
        public void onStateChanged(UdpBridgeService.BridgeState newState) {
            runOnUiThread(() -> updateBridgeUI());
        }

        @Override
        public void onClientConnected(int clientId) {
            runOnUiThread(() -> updateBridgeStats());
        }

        @Override
        public void onClientDisconnected(int clientId) {
            runOnUiThread(() -> updateBridgeStats());
        }

        @Override
        public void onDataTransferred(long bytes) {
            runOnUiThread(() -> updateBridgeStats());
        }

        @Override
        public void onError(String error) {
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "Bridge Error: " + error, Toast.LENGTH_LONG).show();
                updateBridgeUI();
            });
        }
    };

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
        
        // Bind to UDP Bridge service
        Intent bridgeIntent = new Intent(this, UdpBridgeService.class);
        bindService(bridgeIntent, bridgeServiceConnection, Context.BIND_AUTO_CREATE);
    }
    
    private void initializeViews() {
        // New simplified UI
        connectButton = findViewById(R.id.connect_button);
        bridgeStatsTextView = findViewById(R.id.bridge_stats_text_view);
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
        
        String host = currentServerConfig.getSshHost();
        int port = currentServerConfig.getSshPort();
        String username = currentServerConfig.getUsername();
        boolean useKeyAuth = currentServerConfig.isUsePrivateKey();

        // Collect forwarding config to auto-start after connect
        Integer localPort = null;
        Integer remotePort = null;
        String remoteHost = currentServerConfig.getRemoteHost();
        
        if (currentServerConfig.getLocalUdpPort() > 0 && currentServerConfig.getRemoteUdpPort() > 0) {
            localPort = currentServerConfig.getLocalUdpPort();
            remotePort = currentServerConfig.getRemoteUdpPort();
        }
        
        // Provide pending forwarding to service (best-effort)
        sshTunnelService.setPendingForwarding(localPort, remoteHost, remotePort);

        // Final copies for inner classes
        final Integer fLocalPort = localPort;
        final Integer fRemotePort = remotePort;
        final String fRemoteHost = remoteHost;

        new Thread(() -> {
            boolean connected;
            
            if (useKeyAuth) {
                String privateKeyPath = currentServerConfig.getPrivateKeyPath();
                String passphrase = currentServerConfig.getPassphrase();
                connected = sshTunnelService.connectWithPrivateKey(host, port, username, privateKeyPath, 
                                                                 passphrase);
            } else {
                String password = currentServerConfig.getPassword();
                connected = sshTunnelService.connect(host, port, username, password);
            }
            
            runOnUiThread(() -> {
                if (connected) {
                    String authMethod = useKeyAuth ? "private key" : "password";
                    String status = "Connected (" + authMethod + ")";
                    // If forwarding config present, update status meaningfully
                    if (fLocalPort != null && fRemotePort != null && fRemoteHost != null && !fRemoteHost.isEmpty()) {
                        status = "Forwarding " + fLocalPort + " -> " + fRemoteHost + ":" + fRemotePort + " (" + authMethod + ")";
                    }
                    Toast.makeText(MainActivity.this, "Connected to SSH server using " + authMethod, Toast.LENGTH_SHORT).show();
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
        
        int localPort = currentServerConfig.getLocalUdpPort();
        String remoteHost = currentServerConfig.getRemoteHost();
        int remotePort = currentServerConfig.getRemoteUdpPort();
        
        new Thread(() -> {
            boolean success = sshTunnelService.startUdpForwarding(localPort, remoteHost, remotePort);
            
            runOnUiThread(() -> {
                if (success) {
                    Toast.makeText(MainActivity.this, "UDP forwarding started", Toast.LENGTH_SHORT).show();
                    bridgeStatsTextView.setText("Forwarding " + localPort + " -> " + remoteHost + ":" + remotePort);
                } else {
                    Toast.makeText(MainActivity.this, "Failed to start UDP forwarding", Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }
    
    // UDP Bridge methods
    
    private void startUdpBridge() {
        if (!bridgeServiceBound) {
            Toast.makeText(this, "Bridge service not available", Toast.LENGTH_SHORT).show();
            return;
        }
        
        if (currentServerConfig == null || !currentServerConfig.isBridgeEnabled()) {
            Toast.makeText(this, "Bridge not configured for selected server", Toast.LENGTH_SHORT).show();
            return;
        }
        
        // Create UDP bridge config from current server config
        UdpBridgeConfig udpBridgeConfig = new UdpBridgeConfig(this);
        udpBridgeConfig.setBridgeEnabled(true);
        udpBridgeConfig.setBridgeHost(currentServerConfig.getBridgeHost());
        udpBridgeConfig.setBridgePort(currentServerConfig.getBridgePort());
        udpBridgeConfig.setLocalPort(currentServerConfig.getLocalPort());
        udpBridgeConfig.setAutoReconnectEnabled(currentServerConfig.isAutoReconnect());
        udpBridgeConfig.setConnectionTimeout(currentServerConfig.getConnectionTimeout());
        
        // Validate configuration
        if (!udpBridgeConfig.isValid()) {
            Toast.makeText(this, "Invalid bridge configuration", Toast.LENGTH_LONG).show();
            return;
        }
        
        // Check if SSH tunnel is needed
        if (udpBridgeService.isSshTunnelRequired() && !udpBridgeService.isSshTunnelAvailable()) {
            Toast.makeText(this, "SSH tunnel required but not connected. Please establish SSH connection first.", Toast.LENGTH_LONG).show();
            return;
        }
        
        // Start bridge
        if (udpBridgeService.startBridge()) {
            Toast.makeText(this, "Starting UDP Bridge...", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "Failed to start UDP Bridge", Toast.LENGTH_SHORT).show();
        }
    }
    
    private void stopUdpBridge() {
        if (!bridgeServiceBound) {
            return;
        }
        
        udpBridgeService.stopBridge();
        Toast.makeText(this, "Stopping UDP Bridge...", Toast.LENGTH_SHORT).show();
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
                android.util.Log.d("MainActivity", "Received server config with ID: " + serverConfig.getId() + ", isEditMode: " + isEditMode);
                
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
        if (serviceBound) {
            unbindService(serviceConnection);
        }
        if (bridgeServiceBound) {
            unbindService(bridgeServiceConnection);
        }
        super.onDestroy();
    }
    
    private void updateBridgeUI() {
        if (!bridgeServiceBound) {
            return;
        }
        
        updateBridgeStats();
    }
    
    private void updateBridgeStats() {
        if (!bridgeServiceBound) {
            return;
        }
        
        String stats = udpBridgeService.getStatisticsString();
        if (stats != null && !stats.isEmpty()) {
            bridgeStatsTextView.setText(stats);
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
}