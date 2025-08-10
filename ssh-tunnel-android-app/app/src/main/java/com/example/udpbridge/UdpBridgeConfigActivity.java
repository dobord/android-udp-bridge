package com.example.udpbridge;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

import com.example.sshtunnel.R;

/**
 * Activity for configuring UDP Bridge settings
 */
public class UdpBridgeConfigActivity extends AppCompatActivity {
    
    private UdpBridgeConfig config;
    
    private SwitchCompat bridgeEnabledSwitch;
    private EditText bridgeHostEditText;
    private EditText bridgePortEditText;
    private EditText localPortEditText;
    private SwitchCompat autoReconnectSwitch;
    private EditText connectionTimeoutEditText;
    private Button saveButton;
    private Button resetButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_udp_bridge_config);
        
        config = new UdpBridgeConfig(this);
        
        initializeViews();
        loadSettings();
        setupClickListeners();
    }
    
    private void initializeViews() {
        bridgeEnabledSwitch = findViewById(R.id.config_bridge_enabled_switch);
        bridgeHostEditText = findViewById(R.id.config_bridge_host_edit_text);
        bridgePortEditText = findViewById(R.id.config_bridge_port_edit_text);
        localPortEditText = findViewById(R.id.config_local_port_edit_text);
        autoReconnectSwitch = findViewById(R.id.config_auto_reconnect_switch);
        connectionTimeoutEditText = findViewById(R.id.config_connection_timeout_edit_text);
        saveButton = findViewById(R.id.config_save_button);
        resetButton = findViewById(R.id.config_reset_button);
    }
    
    private void loadSettings() {
        bridgeEnabledSwitch.setChecked(config.isBridgeEnabled());
        bridgeHostEditText.setText(config.getBridgeHost());
        bridgePortEditText.setText(String.valueOf(config.getBridgePort()));
        localPortEditText.setText(String.valueOf(config.getLocalPort()));
        autoReconnectSwitch.setChecked(config.isAutoReconnectEnabled());
        connectionTimeoutEditText.setText(String.valueOf(config.getConnectionTimeout()));
    }
    
    private void setupClickListeners() {
        saveButton.setOnClickListener(v -> saveSettings());
        resetButton.setOnClickListener(v -> resetSettings());
    }
    
    private void saveSettings() {
        try {
            // Validate and save settings
            String hostText = bridgeHostEditText.getText().toString().trim();
            if (hostText.isEmpty()) {
                Toast.makeText(this, "Bridge host cannot be empty", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String portText = bridgePortEditText.getText().toString().trim();
            int bridgePort = Integer.parseInt(portText);
            if (bridgePort <= 0 || bridgePort > 65535) {
                Toast.makeText(this, "Invalid bridge port", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String localPortText = localPortEditText.getText().toString().trim();
            int localPort = Integer.parseInt(localPortText);
            if (localPort <= 0 || localPort > 65535) {
                Toast.makeText(this, "Invalid local port", Toast.LENGTH_SHORT).show();
                return;
            }
            
            String timeoutText = connectionTimeoutEditText.getText().toString().trim();
            int timeout = Integer.parseInt(timeoutText);
            if (timeout <= 0) {
                Toast.makeText(this, "Invalid connection timeout", Toast.LENGTH_SHORT).show();
                return;
            }
            
            // Save settings
            config.setBridgeEnabled(bridgeEnabledSwitch.isChecked());
            config.setBridgeHost(hostText);
            config.setBridgePort(bridgePort);
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
    
    private void resetSettings() {
        config.resetToDefaults();
        loadSettings();
        Toast.makeText(this, "Settings reset to defaults", Toast.LENGTH_SHORT).show();
    }
}
