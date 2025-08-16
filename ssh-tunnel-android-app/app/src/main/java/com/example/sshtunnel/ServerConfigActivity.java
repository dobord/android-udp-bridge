package com.example.sshtunnel;

import android.content.Intent;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.RadioGroup;
import android.widget.Toast;
import androidx.appcompat.app.AlertDialog;
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
    private TextInputEditText remoteBridgeHostEditText;
    private TextInputEditText remoteBridgePortEditText;
    private TextInputEditText localBridgePortEditText;
    private TextInputEditText localBridgeHostEditText;
    private SwitchMaterial autoReconnectSwitch;
    private TextInputEditText connectionTimeoutEditText;
    
    // Advanced UDP settings
    private TextInputEditText localUdpPortEditText;
    private TextInputEditText localUdpHostEditText;
    private TextInputEditText remoteUdpHostEditText;
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
        
    remoteBridgeHostEditText = findViewById(R.id.bridge_host_edit_text);
    remoteBridgePortEditText = findViewById(R.id.bridge_port_edit_text);
    localBridgePortEditText = findViewById(R.id.local_port_edit_text);
    localBridgeHostEditText = findViewById(R.id.local_bridge_host_edit_text);
        autoReconnectSwitch = findViewById(R.id.auto_reconnect_switch);
        connectionTimeoutEditText = findViewById(R.id.connection_timeout_edit_text);
        
    localUdpPortEditText = findViewById(R.id.local_udp_port_edit_text);
    localUdpHostEditText = findViewById(R.id.local_udp_host_edit_text);
    remoteUdpHostEditText = findViewById(R.id.remote_host_edit_text);
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
    }

    private void loadServerConfig() {
        Intent intent = getIntent();
        isEditMode = intent.getBooleanExtra(EXTRA_IS_EDIT_MODE, false);
        
        android.util.Log.d("ServerConfigActivity", "Loading server config, isEditMode: " + isEditMode);
        
        if (isEditMode) {
            serverConfig = intent.getParcelableExtra(EXTRA_SERVER_CONFIG);
            android.util.Log.d("ServerConfigActivity", "Loaded server config with ID: " + (serverConfig != null ? serverConfig.getId() : "null"));
            setTitle("Edit Server");
        } else {
            serverConfig = new ServerConfig();
            android.util.Log.d("ServerConfigActivity", "Created new server config with ID: " + serverConfig.getId());
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
    remoteBridgeHostEditText.setText(serverConfig.getRemoteBridgeHost());
    remoteBridgePortEditText.setText(String.valueOf(serverConfig.getRemoteBridgePort()));
    localBridgePortEditText.setText(String.valueOf(serverConfig.getLocalBridgePort()));
    {
        String lb = serverConfig.getLocalBridgeHost();
        localBridgeHostEditText.setText((lb != null && !lb.isEmpty()) ? lb : "127.0.0.1");
    }
        autoReconnectSwitch.setChecked(serverConfig.isAutoReconnect());
        connectionTimeoutEditText.setText(String.valueOf(serverConfig.getConnectionTimeout()));
        
    // UDP settings
        if (serverConfig.getLocalUdpPort() > 0) {
            localUdpPortEditText.setText(String.valueOf(serverConfig.getLocalUdpPort()));
        }
    String luh = serverConfig.getLocalUdpHost();
    localUdpHostEditText.setText((luh != null && !luh.isEmpty()) ? luh : "127.0.0.1");
    String ruh = serverConfig.getRemoteUdpHost();
    remoteUdpHostEditText.setText((ruh != null && !ruh.isEmpty()) ? ruh : "127.0.0.1");
        if (serverConfig.getRemoteUdpPort() > 0) {
            remoteUdpPortEditText.setText(String.valueOf(serverConfig.getRemoteUdpPort()));
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.server_config_menu, menu);
        
        // Hide delete option for new servers
        MenuItem deleteItem = menu.findItem(R.id.action_delete);
        if (deleteItem != null) {
            deleteItem.setVisible(isEditMode);
        }
        
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
        } else if (itemId == R.id.action_delete) {
            deleteServerConfig();
            return true;
        } else {
            return super.onOptionsItemSelected(item);
        }
    }

    private void saveServerConfig() {
        if (!validateFields()) {
            return;
        }
        
        // Debug: Log the current ID
        android.util.Log.d("ServerConfig", "Saving server with ID: " + serverConfig.getId() + ", isEditMode: " + isEditMode);
        
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
    serverConfig.setRemoteBridgeHost(remoteBridgeHostEditText.getText().toString().trim());
    serverConfig.setRemoteBridgePort(Integer.parseInt(remoteBridgePortEditText.getText().toString().trim()));
    serverConfig.setLocalBridgePort(Integer.parseInt(localBridgePortEditText.getText().toString().trim()));
    serverConfig.setLocalBridgeHost(localBridgeHostEditText.getText().toString().trim());
        serverConfig.setConnectionTimeout(Integer.parseInt(connectionTimeoutEditText.getText().toString().trim()));
        serverConfig.setAutoReconnect(autoReconnectSwitch.isChecked());
        
        // Save UDP settings
    String localUdpPortStr = localUdpPortEditText.getText().toString().trim();
        if (!localUdpPortStr.isEmpty()) {
            serverConfig.setLocalUdpPort(Integer.parseInt(localUdpPortStr));
        }
    serverConfig.setLocalUdpHost(localUdpHostEditText.getText().toString().trim());
    serverConfig.setRemoteUdpHost(remoteUdpHostEditText.getText().toString().trim());
        
        String remoteUdpPortStr = remoteUdpPortEditText.getText().toString().trim();
        if (!remoteUdpPortStr.isEmpty()) {
            serverConfig.setRemoteUdpPort(Integer.parseInt(remoteUdpPortStr));
        }
        
        // Return result
        Intent resultIntent = new Intent();
        resultIntent.putExtra(EXTRA_SERVER_CONFIG, serverConfig);
        resultIntent.putExtra(EXTRA_IS_EDIT_MODE, isEditMode);
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
        
        // Validate bridge settings (always required now)
        if (remoteBridgeHostEditText.getText().toString().trim().isEmpty()) {
            remoteBridgeHostEditText.setError("Bridge host is required");
            remoteBridgeHostEditText.requestFocus();
            return false;
        }
        
        String bridgePortStr = remoteBridgePortEditText.getText().toString().trim();
        if (bridgePortStr.isEmpty()) {
            remoteBridgePortEditText.setError("Bridge port is required");
            remoteBridgePortEditText.requestFocus();
            return false;
        }
        
        try {
            int bridgePort = Integer.parseInt(bridgePortStr);
            if (bridgePort <= 0 || bridgePort > 65535) {
                remoteBridgePortEditText.setError("Port must be between 1 and 65535");
                remoteBridgePortEditText.requestFocus();
                return false;
            }
        } catch (NumberFormatException e) {
            remoteBridgePortEditText.setError("Invalid port number");
            remoteBridgePortEditText.requestFocus();
            return false;
        }
        
        String localPortStr = localBridgePortEditText.getText().toString().trim();
        if (localPortStr.isEmpty()) {
            localBridgePortEditText.setError("Local port is required");
            localBridgePortEditText.requestFocus();
            return false;
        }
        
        try {
            int localPort = Integer.parseInt(localPortStr);
            if (localPort <= 0 || localPort > 65535) {
                localBridgePortEditText.setError("Port must be between 1 and 65535");
                localBridgePortEditText.requestFocus();
                return false;
            }
        } catch (NumberFormatException e) {
            localBridgePortEditText.setError("Invalid port number");
            localBridgePortEditText.requestFocus();
            return false;
        }
        
        return true;
    }
    
    private void deleteServerConfig() {
        if (!isEditMode || serverConfig == null) {
            return;
        }
        
        new AlertDialog.Builder(this)
            .setTitle("Delete Server")
            .setMessage("Are you sure you want to delete this server configuration?")
            .setPositiveButton("Delete", (dialog, which) -> {
                Intent resultIntent = new Intent();
                resultIntent.putExtra("DELETE_SERVER_ID", serverConfig.getId());
                setResult(RESULT_OK, resultIntent);
                finish();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }
}
