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
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    private SshTunnelService sshTunnelService;
    private boolean serviceBound = false;
    
    private EditText hostEditText;
    private EditText portEditText;
    private EditText usernameEditText;
    private EditText passwordEditText;
    private EditText privateKeyEditText;
    private EditText passphraseEditText;
    private EditText localPortEditText;
    private EditText remoteHostEditText;
    private EditText remotePortEditText;
    
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        initializeViews();
        setupClickListeners();
        
        // Bind to service
        Intent intent = new Intent(this, SshTunnelService.class);
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
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
    
    private void updateUI() {
        if (serviceBound) {
            boolean connected = sshTunnelService.isConnected();
            connectButton.setEnabled(!connected);
            disconnectButton.setEnabled(connected);
            startForwardingButton.setEnabled(connected);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (serviceBound) {
            unbindService(serviceConnection);
            serviceBound = false;
        }
    }
}