package com.example.udpbridge;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.RadioGroup;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.button.MaterialButton;

import com.example.sshtunnel.R;

/**
 * Activity for configuring UDP Bridge settings
 */
public class UdpBridgeConfigActivity extends AppCompatActivity {
    
    private UdpBridgeConfig config;
    
    private TextInputEditText bridgeNameEditText;
    private TextInputEditText localPortEditText;
    private TextInputEditText remoteHostEditText;
    private TextInputEditText remotePortEditText;
    private TextInputEditText timeoutEditText;
    private RadioGroup protocolGroup;
    private MaterialSwitch autoReconnectSwitch;
    private MaterialButton saveButton;
    private MaterialButton cancelButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_udp_bridge_config);
        
        config = new UdpBridgeConfig(this);
        
        setupToolbar();
        initializeViews();
        loadSettings();
        setupClickListeners();
    }
    
    private void setupToolbar() {
        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);
        if (getSupportActionBar() != null) {
            getSupportActionBar().setDisplayHomeAsUpEnabled(true);
            getSupportActionBar().setDisplayShowHomeEnabled(true);
        }
    }
    
    private void initializeViews() {
        bridgeNameEditText = findViewById(R.id.bridge_name);
        localPortEditText = findViewById(R.id.local_port);
        remoteHostEditText = findViewById(R.id.remote_host);
        remotePortEditText = findViewById(R.id.remote_port);
        timeoutEditText = findViewById(R.id.timeout);
        protocolGroup = findViewById(R.id.protocol_group);
        autoReconnectSwitch = findViewById(R.id.auto_reconnect);
        saveButton = findViewById(R.id.btn_save);
        cancelButton = findViewById(R.id.btn_cancel);
    }
    
    private void loadSettings() {
        bridgeNameEditText.setText(config.getBridgeName());
        localPortEditText.setText(String.valueOf(config.getLocalPort()));
        remoteHostEditText.setText(config.getBridgeHost());
        remotePortEditText.setText(String.valueOf(config.getBridgePort()));
        timeoutEditText.setText(String.valueOf(config.getConnectionTimeout()));
        autoReconnectSwitch.setChecked(config.isAutoReconnectEnabled());
        
        // Set protocol (UDP is default)
        protocolGroup.check(R.id.protocol_udp);
    }
    
    private void setupClickListeners() {
        saveButton.setOnClickListener(v -> saveSettings());
        cancelButton.setOnClickListener(v -> finish());
    }
    
    private void saveSettings() {
        try {
            // Validate and save settings
            String nameText = bridgeNameEditText.getText().toString().trim();
            if (nameText.isEmpty()) {
                Toast.makeText(this, "Bridge name cannot be empty", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String hostText = remoteHostEditText.getText().toString().trim();
            if (hostText.isEmpty()) {
                Toast.makeText(this, "Remote host cannot be empty", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String remotePortText = remotePortEditText.getText().toString().trim();
            int remotePort = Integer.parseInt(remotePortText);
            if (remotePort <= 0 || remotePort > 65535) {
                Toast.makeText(this, "Invalid remote port", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String localPortText = localPortEditText.getText().toString().trim();
            int localPort = Integer.parseInt(localPortText);
            if (localPort <= 0 || localPort > 65535) {
                Toast.makeText(this, "Invalid local port", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String timeoutText = timeoutEditText.getText().toString().trim();
            int timeout = Integer.parseInt(timeoutText);
            if (timeout <= 0) {
                Toast.makeText(this, "Invalid connection timeout", Toast.LENGTH_SHORT).show();
                return;
            }
            
            // Save settings
            config.setBridgeName(nameText);
            config.setBridgeHost(hostText);
            config.setBridgePort(remotePort);
            config.setLocalPort(localPort);
            config.setAutoReconnectEnabled(autoReconnectSwitch.isChecked());
            config.setConnectionTimeout(timeout);
            
            Toast.makeText(this, "Settings saved successfully", Toast.LENGTH_SHORT).show();
            
            // Return to main activity
            setResult(RESULT_OK);
            finish();
            
        } catch (NumberFormatException e) {
            Toast.makeText(this, "Invalid number format", Toast.LENGTH_SHORT).show();
        }
    }
    
    @Override
    public boolean onSupportNavigateUp() {
        finish();
        return true;
    }
}
