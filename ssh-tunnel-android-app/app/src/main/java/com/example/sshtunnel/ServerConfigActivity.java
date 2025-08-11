package com.example.sshtunnel;

import android.content.Intent;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.RadioGroup;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;
import com.google.android.material.switchmaterial.SwitchMaterial;

import com.example.sshtunnel.models.ServerConfig;

public class ServerConfigActivity extends AppCompatActivity {

    public static final String EXTRA_SERVER_CONFIG = "server_config";
    public static final String EXTRA_IS_EDIT_MODE = "is_edit_mode";

    private ServerConfig serverConfig;
    private boolean isEditMode = false;

    // UI Elements
    private TextInputEditText nameEditText;
    private TextInputEditText hostEditText;
    private TextInputEditText portEditText;
    private TextInputEditText usernameEditText;
    private TextInputEditText passwordEditText;
    private TextInputEditText privateKeyEditText;
    private TextInputEditText passphraseEditText;
    
    private TextInputLayout passwordLayout;
    private TextInputLayout privateKeyLayout;
    private TextInputLayout passphraseLayout;
    
    private RadioGroup authMethodRadioGroup;
    
    // Bridge settings
    private SwitchMaterial bridgeEnabledSwitch;
    private TextInputEditText bridgeHostEditText;
    private TextInputEditText bridgePortEditText;
    private TextInputEditText localPortEditText;
    private SwitchMaterial autoReconnectSwitch;
    private TextInputEditText connectionTimeoutEditText;
    
    // Advanced UDP settings
    private TextInputEditText localUdpPortEditText;
    private TextInputEditText remoteHostEditText;
    private TextInputEditText remoteUdpPortEditText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_server_config);

        setupToolbar();
        initializeViews();
        setupClickListeners();
        loadServerConfig();
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
        nameEditText = findViewById(R.id.name_edit_text);
        hostEditText = findViewById(R.id.host_edit_text);
        portEditText = findViewById(R.id.port_edit_text);
        usernameEditText = findViewById(R.id.username_edit_text);
        passwordEditText = findViewById(R.id.password_edit_text);
        privateKeyEditText = findViewById(R.id.private_key_edit_text);
        passphraseEditText = findViewById(R.id.passphrase_edit_text);
        
        passwordLayout = findViewById(R.id.password_layout);
        privateKeyLayout = findViewById(R.id.private_key_layout);
        passphraseLayout = findViewById(R.id.passphrase_layout);
        
        authMethodRadioGroup = findViewById(R.id.auth_method_radio_group);
        
        bridgeEnabledSwitch = findViewById(R.id.bridge_enabled_switch);
        bridgeHostEditText = findViewById(R.id.bridge_host_edit_text);
        bridgePortEditText = findViewById(R.id.bridge_port_edit_text);
        localPortEditText = findViewById(R.id.local_port_edit_text);
        autoReconnectSwitch = findViewById(R.id.auto_reconnect_switch);
        connectionTimeoutEditText = findViewById(R.id.connection_timeout_edit_text);
        
        localUdpPortEditText = findViewById(R.id.local_udp_port_edit_text);
        remoteHostEditText = findViewById(R.id.remote_host_edit_text);
        remoteUdpPortEditText = findViewById(R.id.remote_udp_port_edit_text);
    }

    private void setupClickListeners() {
        authMethodRadioGroup.setOnCheckedChangeListener((group, checkedId) -> {
            if (checkedId == R.id.password_auth_radio) {
                passwordLayout.setVisibility(View.VISIBLE);
                privateKeyLayout.setVisibility(View.GONE);
                passphraseLayout.setVisibility(View.GONE);
            } else if (checkedId == R.id.key_auth_radio) {
                passwordLayout.setVisibility(View.GONE);
                privateKeyLayout.setVisibility(View.VISIBLE);
                passphraseLayout.setVisibility(View.VISIBLE);
            }
        });
        
        bridgeEnabledSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            updateBridgeFieldsVisibility(isChecked);
        });
    }

    private void updateBridgeFieldsVisibility(boolean enabled) {
        bridgeHostEditText.setEnabled(enabled);
        bridgePortEditText.setEnabled(enabled);
        localPortEditText.setEnabled(enabled);
        autoReconnectSwitch.setEnabled(enabled);
        connectionTimeoutEditText.setEnabled(enabled);
    }

    private void loadServerConfig() {
        Intent intent = getIntent();
        isEditMode = intent.getBooleanExtra(EXTRA_IS_EDIT_MODE, false);
        
        if (isEditMode) {
            serverConfig = intent.getParcelableExtra(EXTRA_SERVER_CONFIG);
            setTitle("Edit Server");
        } else {
            serverConfig = new ServerConfig();
            setTitle("Add Server");
        }
        
        if (serverConfig != null) {
            populateFields();
        }
    }

    private void populateFields() {
        nameEditText.setText(serverConfig.getName());
        hostEditText.setText(serverConfig.getSshHost());
        portEditText.setText(String.valueOf(serverConfig.getSshPort()));
        usernameEditText.setText(serverConfig.getUsername());
        
        if (serverConfig.isUsePrivateKey()) {
            authMethodRadioGroup.check(R.id.key_auth_radio);
            privateKeyEditText.setText(serverConfig.getPrivateKeyPath());
            passphraseEditText.setText(serverConfig.getPassphrase());
        } else {
            authMethodRadioGroup.check(R.id.password_auth_radio);
            passwordEditText.setText(serverConfig.getPassword());
        }
        
        // Bridge settings
        bridgeEnabledSwitch.setChecked(serverConfig.isBridgeEnabled());
        bridgeHostEditText.setText(serverConfig.getBridgeHost());
        bridgePortEditText.setText(String.valueOf(serverConfig.getBridgePort()));
        localPortEditText.setText(String.valueOf(serverConfig.getLocalPort()));
        autoReconnectSwitch.setChecked(serverConfig.isAutoReconnect());
        connectionTimeoutEditText.setText(String.valueOf(serverConfig.getConnectionTimeout()));
        
        // UDP settings
        if (serverConfig.getLocalUdpPort() > 0) {
            localUdpPortEditText.setText(String.valueOf(serverConfig.getLocalUdpPort()));
        }
        remoteHostEditText.setText(serverConfig.getRemoteHost());
        if (serverConfig.getRemoteUdpPort() > 0) {
            remoteUdpPortEditText.setText(String.valueOf(serverConfig.getRemoteUdpPort()));
        }
        
        updateBridgeFieldsVisibility(serverConfig.isBridgeEnabled());
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.server_config_menu, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int itemId = item.getItemId();
        if (itemId == android.R.id.home) {
            finish();
            return true;
        } else if (itemId == R.id.action_save) {
            saveServerConfig();
            return true;
        } else {
            return super.onOptionsItemSelected(item);
        }
    }

    private void saveServerConfig() {
        if (!validateFields()) {
            return;
        }
        
        // Save SSH settings
        serverConfig.setName(nameEditText.getText().toString().trim());
        serverConfig.setSshHost(hostEditText.getText().toString().trim());
        serverConfig.setSshPort(Integer.parseInt(portEditText.getText().toString().trim()));
        serverConfig.setUsername(usernameEditText.getText().toString().trim());
        
        boolean usePrivateKey = authMethodRadioGroup.getCheckedRadioButtonId() == R.id.key_auth_radio;
        serverConfig.setUsePrivateKey(usePrivateKey);
        
        if (usePrivateKey) {
            serverConfig.setPrivateKeyPath(privateKeyEditText.getText().toString().trim());
            serverConfig.setPassphrase(passphraseEditText.getText().toString().trim());
            serverConfig.setPassword(null);
        } else {
            serverConfig.setPassword(passwordEditText.getText().toString().trim());
            serverConfig.setPrivateKeyPath(null);
            serverConfig.setPassphrase(null);
        }
        
        // Save bridge settings
        serverConfig.setBridgeEnabled(bridgeEnabledSwitch.isChecked());
        if (bridgeEnabledSwitch.isChecked()) {
            serverConfig.setBridgeHost(bridgeHostEditText.getText().toString().trim());
            serverConfig.setBridgePort(Integer.parseInt(bridgePortEditText.getText().toString().trim()));
            serverConfig.setLocalPort(Integer.parseInt(localPortEditText.getText().toString().trim()));
            serverConfig.setConnectionTimeout(Integer.parseInt(connectionTimeoutEditText.getText().toString().trim()));
        }
        serverConfig.setAutoReconnect(autoReconnectSwitch.isChecked());
        
        // Save UDP settings
        String localUdpPortStr = localUdpPortEditText.getText().toString().trim();
        if (!localUdpPortStr.isEmpty()) {
            serverConfig.setLocalUdpPort(Integer.parseInt(localUdpPortStr));
        }
        
        serverConfig.setRemoteHost(remoteHostEditText.getText().toString().trim());
        
        String remoteUdpPortStr = remoteUdpPortEditText.getText().toString().trim();
        if (!remoteUdpPortStr.isEmpty()) {
            serverConfig.setRemoteUdpPort(Integer.parseInt(remoteUdpPortStr));
        }
        
        // Return result
        Intent resultIntent = new Intent();
        resultIntent.putExtra(EXTRA_SERVER_CONFIG, serverConfig);
        setResult(RESULT_OK, resultIntent);
        finish();
    }

    private boolean validateFields() {
        // Validate required SSH fields
        if (nameEditText.getText().toString().trim().isEmpty()) {
            nameEditText.setError("Server name is required");
            nameEditText.requestFocus();
            return false;
        }
        
        if (hostEditText.getText().toString().trim().isEmpty()) {
            hostEditText.setError("SSH host is required");
            hostEditText.requestFocus();
            return false;
        }
        
        String portStr = portEditText.getText().toString().trim();
        if (portStr.isEmpty()) {
            portEditText.setError("Port is required");
            portEditText.requestFocus();
            return false;
        }
        
        try {
            int port = Integer.parseInt(portStr);
            if (port <= 0 || port > 65535) {
                portEditText.setError("Port must be between 1 and 65535");
                portEditText.requestFocus();
                return false;
            }
        } catch (NumberFormatException e) {
            portEditText.setError("Invalid port number");
            portEditText.requestFocus();
            return false;
        }
        
        if (usernameEditText.getText().toString().trim().isEmpty()) {
            usernameEditText.setError("Username is required");
            usernameEditText.requestFocus();
            return false;
        }
        
        // Validate authentication
        boolean usePrivateKey = authMethodRadioGroup.getCheckedRadioButtonId() == R.id.key_auth_radio;
        if (usePrivateKey) {
            if (privateKeyEditText.getText().toString().trim().isEmpty()) {
                privateKeyEditText.setError("Private key path is required");
                privateKeyEditText.requestFocus();
                return false;
            }
        } else {
            if (passwordEditText.getText().toString().trim().isEmpty()) {
                passwordEditText.setError("Password is required");
                passwordEditText.requestFocus();
                return false;
            }
        }
        
        // Validate bridge settings if enabled
        if (bridgeEnabledSwitch.isChecked()) {
            if (bridgeHostEditText.getText().toString().trim().isEmpty()) {
                bridgeHostEditText.setError("Bridge host is required");
                bridgeHostEditText.requestFocus();
                return false;
            }
            
            String bridgePortStr = bridgePortEditText.getText().toString().trim();
            if (bridgePortStr.isEmpty()) {
                bridgePortEditText.setError("Bridge port is required");
                bridgePortEditText.requestFocus();
                return false;
            }
            
            try {
                int bridgePort = Integer.parseInt(bridgePortStr);
                if (bridgePort <= 0 || bridgePort > 65535) {
                    bridgePortEditText.setError("Port must be between 1 and 65535");
                    bridgePortEditText.requestFocus();
                    return false;
                }
            } catch (NumberFormatException e) {
                bridgePortEditText.setError("Invalid port number");
                bridgePortEditText.requestFocus();
                return false;
            }
            
            String localPortStr = localPortEditText.getText().toString().trim();
            if (localPortStr.isEmpty()) {
                localPortEditText.setError("Local port is required");
                localPortEditText.requestFocus();
                return false;
            }
            
            try {
                int localPort = Integer.parseInt(localPortStr);
                if (localPort <= 0 || localPort > 65535) {
                    localPortEditText.setError("Port must be between 1 and 65535");
                    localPortEditText.requestFocus();
                    return false;
                }
            } catch (NumberFormatException e) {
                localPortEditText.setError("Invalid port number");
                localPortEditText.requestFocus();
                return false;
            }
        }
        
        return true;
    }
}
