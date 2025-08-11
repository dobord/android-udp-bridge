package com.example.sshtunnel;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioGroup;
import android.widget.RadioButton;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

import com.example.udpbridge.UdpBridgeConfig;
import com.example.udpbridge.UdpBridgeService;
import com.example.udpbridge.UdpBridgeConfigActivity;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_CODE_BRIDGE_CONFIG = 1001;

    private SshTunnelService sshTunnelService;
    private UdpBridgeService udpBridgeService;
    private UdpBridgeConfig udpBridgeConfig;
    private boolean serviceBound = false;
    private boolean bridgeServiceBound = false;
    
    private EditText hostEditText;
    private EditText portEditText;
    private EditText usernameEditText;
    private EditText passwordEditText;
    private EditText privateKeyEditText;
    private EditText passphraseEditText;
    private EditText localPortEditText;
    private EditText remoteHostEditText;
    private EditText remotePortEditText;
    
    // UDP Bridge UI elements
    private SwitchCompat bridgeEnabledSwitch;
    private EditText bridgeHostEditText;
    private EditText bridgePortEditText;
    private EditText bridgeLocalPortEditText;
    private SwitchCompat autoReconnectSwitch;
    private EditText connectionTimeoutEditText;
    private Button bridgeStartButton;
    private Button bridgeStopButton;
    private Button bridgeConfigButton;
    private TextView bridgeStatusTextView;
    private TextView bridgeStatsTextView;
    
    private RadioGroup authMethodRadioGroup;
    private RadioButton passwordAuthRadio;
    private RadioButton keyAuthRadio;
    
    private Button connectButton;
    private Button disconnectButton;
    private Button startForwardingButton;
    private TextView statusTextView;

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

        // Initialize UDP Bridge config
        udpBridgeConfig = new UdpBridgeConfig(this);

        initializeViews();
        setupClickListeners();
        loadBridgeSettings();
        
        // Bind to SSH service
        Intent intent = new Intent(this, SshTunnelService.class);
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
        
        // Bind to UDP Bridge service
        Intent bridgeIntent = new Intent(this, UdpBridgeService.class);
        bindService(bridgeIntent, bridgeServiceConnection, Context.BIND_AUTO_CREATE);
    }
    
    private void initializeViews() {
        hostEditText = findViewById(R.id.host_edit_text);
        portEditText = findViewById(R.id.port_edit_text);
        usernameEditText = findViewById(R.id.username_edit_text);
        passwordEditText = findViewById(R.id.password_edit_text);
        privateKeyEditText = findViewById(R.id.private_key_edit_text);
        passphraseEditText = findViewById(R.id.passphrase_edit_text);
        localPortEditText = findViewById(R.id.local_port_edit_text);
        remoteHostEditText = findViewById(R.id.remote_host_edit_text);
        remotePortEditText = findViewById(R.id.remote_port_edit_text);
        
        authMethodRadioGroup = findViewById(R.id.auth_method_radio_group);
        passwordAuthRadio = findViewById(R.id.password_auth_radio);
        keyAuthRadio = findViewById(R.id.key_auth_radio);
        
        connectButton = findViewById(R.id.connect_button);
        disconnectButton = findViewById(R.id.disconnect_button);
        startForwardingButton = findViewById(R.id.start_forwarding_button);
        statusTextView = findViewById(R.id.status_text_view);
        
        // UDP Bridge elements
        bridgeEnabledSwitch = findViewById(R.id.bridge_enabled_switch);
        bridgeHostEditText = findViewById(R.id.bridge_host_edit_text);
        bridgePortEditText = findViewById(R.id.bridge_port_edit_text);
        bridgeLocalPortEditText = findViewById(R.id.bridge_local_port_edit_text);
        autoReconnectSwitch = findViewById(R.id.auto_reconnect_switch);
        connectionTimeoutEditText = findViewById(R.id.connection_timeout_edit_text);
        bridgeStartButton = findViewById(R.id.bridge_start_button);
        bridgeStopButton = findViewById(R.id.bridge_stop_button);
        bridgeConfigButton = findViewById(R.id.bridge_config_button);
        bridgeStatusTextView = findViewById(R.id.bridge_status_text_view);
        bridgeStatsTextView = findViewById(R.id.bridge_stats_text_view);
        
        // Set default values
        hostEditText.setText("192.168.1.100");
        portEditText.setText("22");
        usernameEditText.setText("user");
        localPortEditText.setText("8080");
        remoteHostEditText.setText("127.0.0.1");
        remotePortEditText.setText("80");
    }
    
    private void setupClickListeners() {
        authMethodRadioGroup.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(RadioGroup group, int checkedId) {
                if (checkedId == R.id.password_auth_radio) {
                    // Show password fields, hide key fields
                    passwordEditText.setVisibility(View.VISIBLE);
                    privateKeyEditText.setVisibility(View.GONE);
                    passphraseEditText.setVisibility(View.GONE);
                } else if (checkedId == R.id.key_auth_radio) {
                    // Hide password fields, show key fields
                    passwordEditText.setVisibility(View.GONE);
                    privateKeyEditText.setVisibility(View.VISIBLE);
                    passphraseEditText.setVisibility(View.VISIBLE);
                }
            }
        });
        
        connectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                connectToSshServer();
            }
        });

        disconnectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                disconnectFromSshServer();
            }
        });
        
        startForwardingButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startUdpForwarding();
            }
        });
        
        // UDP Bridge listeners
        bridgeEnabledSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            udpBridgeConfig.setBridgeEnabled(isChecked);
            updateBridgeUIState();
        });
        
        autoReconnectSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            udpBridgeConfig.setAutoReconnectEnabled(isChecked);
        });
        
        bridgeStartButton.setOnClickListener(v -> startUdpBridge());
        bridgeStopButton.setOnClickListener(v -> stopUdpBridge());
        bridgeConfigButton.setOnClickListener(v -> openBridgeConfig());
    }

    private void connectToSshServer() {
        if (!serviceBound) {
            Toast.makeText(this, "Service not available", Toast.LENGTH_SHORT).show();
            return;
        }
        
        String host = hostEditText.getText().toString().trim();
        String portStr = portEditText.getText().toString().trim();
        String username = usernameEditText.getText().toString().trim();
        
        if (host.isEmpty() || portStr.isEmpty() || username.isEmpty()) {
            Toast.makeText(this, "Please fill all required fields", Toast.LENGTH_SHORT).show();
            return;
        }
        
        boolean useKeyAuth = keyAuthRadio.isChecked();

        // Collect forwarding config to auto-start after connect
        String localPortStr = localPortEditText.getText().toString().trim();
        String remoteHost = remoteHostEditText.getText().toString().trim();
        String remotePortStr = remotePortEditText.getText().toString().trim();
        Integer localPort = null;
        Integer remotePort = null;
        if (!localPortStr.isEmpty() && !remoteHost.isEmpty() && !remotePortStr.isEmpty()) {
            try {
                localPort = Integer.parseInt(localPortStr);
                remotePort = Integer.parseInt(remotePortStr);
            } catch (NumberFormatException ignore) { /* validated later */ }
        }
        
        if (useKeyAuth) {
            String privateKeyPath = privateKeyEditText.getText().toString().trim();
            if (privateKeyPath.isEmpty()) {
                Toast.makeText(this, "Please specify private key path", Toast.LENGTH_SHORT).show();
                return;
            }
        } else {
            String password = passwordEditText.getText().toString().trim();
            if (password.isEmpty()) {
                Toast.makeText(this, "Please enter password", Toast.LENGTH_SHORT).show();
                return;
            }
        }
        
        try {
            int port = Integer.parseInt(portStr);
            
            // Provide pending forwarding to service (best-effort)
            sshTunnelService.setPendingForwarding(localPort, remoteHost, remotePort);

            // Final copies for inner classes
            final Integer fLocalPort = localPort;
            final Integer fRemotePort = remotePort;
            final String fRemoteHost = remoteHost;
            final boolean useKeyAuthLocal = useKeyAuth;

            new Thread(new Runnable() {
                @Override
                public void run() {
                    boolean connected;
                    
                    if (useKeyAuthLocal) {
                        String privateKeyPath = privateKeyEditText.getText().toString().trim();
                        String passphrase = passphraseEditText.getText().toString().trim();
                        connected = sshTunnelService.connectWithPrivateKey(host, port, username, privateKeyPath, 
                                                                         passphrase.isEmpty() ? null : passphrase);
                    } else {
                        String password = passwordEditText.getText().toString().trim();
                        connected = sshTunnelService.connect(host, port, username, password);
                    }
                    
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (connected) {
                                String authMethod = useKeyAuthLocal ? "private key" : "password";
                                String status = "Status: Connected (" + authMethod + ")";
                                // If forwarding config present, update status meaningfully
                                if (fLocalPort != null && fRemotePort != null && fRemoteHost != null && !fRemoteHost.isEmpty()) {
                                    status = "Status: Forwarding " + fLocalPort + " -> " + fRemoteHost + ":" + fRemotePort + " (" + authMethod + ")";
                                }
                                Toast.makeText(MainActivity.this, "Connected to SSH server using " + authMethod, Toast.LENGTH_SHORT).show();
                                statusTextView.setText(status);
                            } else {
                                Toast.makeText(MainActivity.this, "Failed to connect", Toast.LENGTH_SHORT).show();
                                statusTextView.setText("Status: Connection failed");
                            }
                            updateUI();
                        }
                    });
                }
            }).start();
            
        } catch (NumberFormatException e) {
            Toast.makeText(this, "Invalid port number", Toast.LENGTH_SHORT).show();
        }
    }
    
    private void disconnectFromSshServer() {
        if (serviceBound) {
            sshTunnelService.disconnectFromServer();
            statusTextView.setText("Status: Disconnected");
            updateUI();
            Toast.makeText(this, "Disconnected from SSH server", Toast.LENGTH_SHORT).show();
        }
    }
    
    private void startUdpForwarding() {
        if (!serviceBound || !sshTunnelService.isConnected()) {
            Toast.makeText(this, "Not connected to SSH server", Toast.LENGTH_SHORT).show();
            return;
        }
        
        String localPortStr = localPortEditText.getText().toString().trim();
        String remoteHost = remoteHostEditText.getText().toString().trim();
        String remotePortStr = remotePortEditText.getText().toString().trim();
        
        if (localPortStr.isEmpty() || remoteHost.isEmpty() || remotePortStr.isEmpty()) {
            Toast.makeText(this, "Please fill all forwarding fields", Toast.LENGTH_SHORT).show();
            return;
        }
        
        try {
            int localPort = Integer.parseInt(localPortStr);
            int remotePort = Integer.parseInt(remotePortStr);
            
            new Thread(new Runnable() {
                @Override
                public void run() {
                    boolean success = sshTunnelService.startUdpForwarding(localPort, remoteHost, remotePort);
                    
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (success) {
                                Toast.makeText(MainActivity.this, "UDP forwarding started", Toast.LENGTH_SHORT).show();
                                statusTextView.setText("Status: Forwarding " + localPort + " -> " + remoteHost + ":" + remotePort);
                            } else {
                                Toast.makeText(MainActivity.this, "Failed to start UDP forwarding", Toast.LENGTH_SHORT).show();
                            }
                        }
                    });
                }
            }).start();
            
        } catch (NumberFormatException e) {
            Toast.makeText(this, "Invalid port number", Toast.LENGTH_SHORT).show();
        }
    }
    
    // UDP Bridge methods
    
    private void loadBridgeSettings() {
        bridgeEnabledSwitch.setChecked(udpBridgeConfig.isBridgeEnabled());
        bridgeHostEditText.setText(udpBridgeConfig.getBridgeHost());
        bridgePortEditText.setText(String.valueOf(udpBridgeConfig.getBridgePort()));
        bridgeLocalPortEditText.setText(String.valueOf(udpBridgeConfig.getLocalPort()));
        autoReconnectSwitch.setChecked(udpBridgeConfig.isAutoReconnectEnabled());
        connectionTimeoutEditText.setText(String.valueOf(udpBridgeConfig.getConnectionTimeout()));
        
        updateBridgeUIState();
    }
    
    private void saveBridgeSettings() {
        try {
            udpBridgeConfig.setBridgeHost(bridgeHostEditText.getText().toString().trim());
            
            String portStr = bridgePortEditText.getText().toString().trim();
            if (!portStr.isEmpty()) {
                udpBridgeConfig.setBridgePort(Integer.parseInt(portStr));
            }
            
            String localPortStr = bridgeLocalPortEditText.getText().toString().trim();
            if (!localPortStr.isEmpty()) {
                udpBridgeConfig.setLocalPort(Integer.parseInt(localPortStr));
            }
            
            String timeoutStr = connectionTimeoutEditText.getText().toString().trim();
            if (!timeoutStr.isEmpty()) {
                udpBridgeConfig.setConnectionTimeout(Integer.parseInt(timeoutStr));
            }
        } catch (NumberFormatException e) {
            Toast.makeText(this, "Invalid number format in bridge settings", Toast.LENGTH_SHORT).show();
        }
    }
    
    private void updateBridgeUIState() {
        boolean enabled = bridgeEnabledSwitch.isChecked();
        
        bridgeHostEditText.setEnabled(enabled);
        bridgePortEditText.setEnabled(enabled);
        bridgeLocalPortEditText.setEnabled(enabled);
        autoReconnectSwitch.setEnabled(enabled);
        connectionTimeoutEditText.setEnabled(enabled);
        
        if (bridgeServiceBound && enabled) {
            UdpBridgeService.BridgeState state = udpBridgeService.getCurrentState();
            bridgeStartButton.setEnabled(state == UdpBridgeService.BridgeState.DISCONNECTED);
            bridgeStopButton.setEnabled(state != UdpBridgeService.BridgeState.DISCONNECTED);
        } else {
            bridgeStartButton.setEnabled(false);
            bridgeStopButton.setEnabled(false);
        }
    }
    
    private void updateBridgeUI() {
        if (!bridgeServiceBound) {
            bridgeStatusTextView.setText("Bridge Status: Service not connected");
            bridgeStatsTextView.setText("Statistics: N/A");
            return;
        }
        
        UdpBridgeService.BridgeState state = udpBridgeService.getCurrentState();
        bridgeStatusTextView.setText("Bridge Status: " + state.name());
        
        updateBridgeStats();
        updateBridgeUIState();
    }
    
    private void updateBridgeStats() {
        if (!bridgeServiceBound) {
            return;
        }
        
        String stats = udpBridgeService.getStatisticsString();
        bridgeStatsTextView.setText("Statistics:\n" + stats);
    }
    
    private void startUdpBridge() {
        if (!bridgeServiceBound) {
            Toast.makeText(this, "Bridge service not available", Toast.LENGTH_SHORT).show();
            return;
        }
        
        // Save current settings
        saveBridgeSettings();
        
        // Validate configuration
        if (!udpBridgeConfig.isValid()) {
            Toast.makeText(this, "Invalid bridge configuration. Please check all fields.", Toast.LENGTH_LONG).show();
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

    private void openBridgeConfig() {
        Intent intent = new Intent(this, UdpBridgeConfigActivity.class);
        startActivityForResult(intent, REQUEST_CODE_BRIDGE_CONFIG);
    }
    
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        
        if (requestCode == REQUEST_CODE_BRIDGE_CONFIG && resultCode == RESULT_OK) {
            // Reload bridge settings after configuration change
            loadBridgeSettings();
            Toast.makeText(this, "Bridge settings updated", Toast.LENGTH_SHORT).show();
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
    
    /**
     * Update all UI elements based on current service states
     */
    private void updateUI() {
        // Update SSH tunnel UI
        if (serviceBound && sshTunnelService != null) {
            if (sshTunnelService.isConnected()) {
                statusTextView.setText("Status: Connected");
            } else {
                statusTextView.setText("Status: Disconnected");
            }
        } else {
            statusTextView.setText("Status: Service not available");
        }
        
        // Update UDP Bridge UI
        updateBridgeUI();
    }
}