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
    private EditText localPortEditText;
    private EditText remoteHostEditText;
    private EditText remotePortEditText;
    
    private Button connectButton;
    private Button disconnectButton;
    private Button startForwardingButton;
    private Button stopForwardingButton;
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
        localPortEditText = findViewById(R.id.local_port_edit_text);
        remoteHostEditText = findViewById(R.id.remote_host_edit_text);
        remotePortEditText = findViewById(R.id.remote_port_edit_text);
        
        connectButton = findViewById(R.id.connect_button);
        disconnectButton = findViewById(R.id.disconnect_button);
        startForwardingButton = findViewById(R.id.start_forwarding_button);
        stopForwardingButton = findViewById(R.id.stop_forwarding_button);
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
        
        stopForwardingButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                stopUdpForwarding();
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
        String password = passwordEditText.getText().toString().trim();
        
        if (host.isEmpty() || portStr.isEmpty() || username.isEmpty() || password.isEmpty()) {
            Toast.makeText(this, "Please fill all connection fields", Toast.LENGTH_SHORT).show();
            return;
        }
        
        try {
            int port = Integer.parseInt(portStr);
            
            new Thread(new Runnable() {
                @Override
                public void run() {
                    boolean connected = sshTunnelService.connect(host, port, username, password);
                    
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (connected) {
                                Toast.makeText(MainActivity.this, "Connected to SSH server", Toast.LENGTH_SHORT).show();
                                statusTextView.setText("Status: Connected");
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
                                Toast.makeText(MainActivity.this, "Port forwarding started", Toast.LENGTH_SHORT).show();
                            } else {
                                Toast.makeText(MainActivity.this, "Failed to start port forwarding", Toast.LENGTH_SHORT).show();
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
    
    private void stopUdpForwarding() {
        if (serviceBound) {
            sshTunnelService.disconnectFromServer();
            
            // Reconnect to server for future use
            String host = hostEditText.getText().toString().trim();
            String portStr = portEditText.getText().toString().trim();
            String username = usernameEditText.getText().toString().trim();
            String password = passwordEditText.getText().toString().trim();
            
            if (!host.isEmpty() && !portStr.isEmpty() && !username.isEmpty() && !password.isEmpty()) {
                try {
                    int port = Integer.parseInt(portStr);
                    new Thread(new Runnable() {
                        @Override
                        public void run() {
                            // Small delay to allow cleanup
                            try { Thread.sleep(1000); } catch (InterruptedException e) {}
                            
                            boolean connected = sshTunnelService.connect(host, port, username, password);
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    if (connected) {
                                        Toast.makeText(MainActivity.this, "Tunnel stopped, SSH connection maintained", Toast.LENGTH_SHORT).show();
                                    } else {
                                        Toast.makeText(MainActivity.this, "Tunnel stopped, SSH connection lost", Toast.LENGTH_SHORT).show();
                                    }
                                    updateUI();
                                }
                            });
                        }
                    }).start();
                } catch (NumberFormatException e) {
                    // Just disconnect without reconnecting
                    Toast.makeText(this, "Tunnel stopped", Toast.LENGTH_SHORT).show();
                    updateUI();
                }
            } else {
                Toast.makeText(this, "Tunnel stopped", Toast.LENGTH_SHORT).show();
                updateUI();
            }
        }
    }
    
    private void updateUI() {
        if (serviceBound && sshTunnelService != null) {
            boolean connected = sshTunnelService.isConnected();
            boolean tunnelActive = sshTunnelService.isTunnelActive();
            
            connectButton.setEnabled(!connected);
            disconnectButton.setEnabled(connected);
            startForwardingButton.setEnabled(connected && !tunnelActive);
            stopForwardingButton.setEnabled(tunnelActive);
            
            // Update status text
            if (tunnelActive) {
                statusTextView.setText("Status: Forwarding " + sshTunnelService.getCurrentTunnelInfo());
                statusTextView.setBackgroundColor(getResources().getColor(android.R.color.holo_green_light));
            } else if (connected) {
                statusTextView.setText("Status: Connected - Ready for forwarding");
                statusTextView.setBackgroundColor(getResources().getColor(android.R.color.holo_blue_light));
            } else {
                statusTextView.setText("Status: Disconnected");
                statusTextView.setBackgroundColor(getResources().getColor(android.R.color.darker_gray));
            }
        } else {
            // Service not bound
            connectButton.setEnabled(false);
            disconnectButton.setEnabled(false);
            startForwardingButton.setEnabled(false);
            stopForwardingButton.setEnabled(false);
            statusTextView.setText("Status: Service not available");
            statusTextView.setBackgroundColor(getResources().getColor(android.R.color.holo_red_light));
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