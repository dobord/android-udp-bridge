package com.example.sshtunnel.models;

import android.content.Context;
import android.content.SharedPreferences;
import com.google.gson.Gson;
import com.google.gson.reflect.TypeToken;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ServerConfigManager {
    private static final String PREFS_NAME = "server_configs";
    private static final String KEY_CONFIGS = "configs";
    private static final String KEY_LAST_SELECTED = "last_selected_id";
    
    private Context context;
    private SharedPreferences prefs;
    private Gson gson;
    private List<ServerConfig> configs;
    
    public ServerConfigManager(Context context) {
        this.context = context;
        this.prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        this.gson = new Gson();
        loadConfigs();
    }
    
    private void loadConfigs() {
        String configsJson = prefs.getString(KEY_CONFIGS, "");
        if (configsJson.isEmpty()) {
            configs = new ArrayList<>();
        } else {
            Type listType = new TypeToken<List<ServerConfig>>(){}.getType();
            configs = gson.fromJson(configsJson, listType);
            if (configs == null) {
                configs = new ArrayList<>();
            }
        }
    }
    
    private void saveConfigs() {
        // Remove any potential duplicates before saving
        removeDuplicates();
        
        String configsJson = gson.toJson(configs);
        prefs.edit().putString(KEY_CONFIGS, configsJson).apply();
        android.util.Log.d("ServerConfigManager", "Saved " + configs.size() + " configs");
    }
    
    private void removeDuplicates() {
        List<ServerConfig> uniqueConfigs = new ArrayList<>();
        Set<Long> seenIds = new HashSet<>();
        
        for (ServerConfig config : configs) {
            if (!seenIds.contains(config.getId())) {
                seenIds.add(config.getId());
                uniqueConfigs.add(config);
            } else {
                android.util.Log.w("ServerConfigManager", "Removed duplicate config with ID: " + config.getId());
            }
        }
        
        configs = uniqueConfigs;
    }
    
    public List<ServerConfig> getAllConfigs() {
        return new ArrayList<>(configs);
    }
    
    public ServerConfig getConfigById(long id) {
        for (ServerConfig config : configs) {
            if (config.getId() == id) {
                return config;
            }
        }
        return null;
    }
    
    public void addConfig(ServerConfig config) {
        // Check if config with this ID already exists
        if (config.getId() != 0) {
            for (ServerConfig existingConfig : configs) {
                if (existingConfig.getId() == config.getId()) {
                    android.util.Log.w("ServerConfigManager", "Attempted to add config that already exists with ID: " + config.getId() + ". Use updateConfig instead.");
                    return;
                }
            }
        }
        
        if (config.getId() == 0) {
            config.setId(System.currentTimeMillis());
        }
        android.util.Log.d("ServerConfigManager", "Adding new config with ID: " + config.getId());
        configs.add(config);
        saveConfigs();
    }
    
    public void updateConfig(ServerConfig config) {
        android.util.Log.d("ServerConfigManager", "Updating config with ID: " + config.getId());
        for (int i = 0; i < configs.size(); i++) {
            android.util.Log.d("ServerConfigManager", "Checking config at index " + i + " with ID: " + configs.get(i).getId());
            if (configs.get(i).getId() == config.getId()) {
                android.util.Log.d("ServerConfigManager", "Found matching config, updating at index: " + i);
                configs.set(i, config);
                saveConfigs();
                return;
            }
        }
        android.util.Log.w("ServerConfigManager", "No matching config found for ID: " + config.getId());
    }
    
    public void deleteConfig(long id) {
        configs.removeIf(config -> config.getId() == id);
        saveConfigs();
        
        // Clear last selected if it was deleted
        if (getLastSelectedConfigId() == id) {
            setLastSelectedConfigId(-1);
        }
    }
    
    public long getLastSelectedConfigId() {
        return prefs.getLong(KEY_LAST_SELECTED, -1);
    }
    
    public void setLastSelectedConfigId(long id) {
        prefs.edit().putLong(KEY_LAST_SELECTED, id).apply();
    }
    
    public ServerConfig getLastSelectedConfig() {
        long id = getLastSelectedConfigId();
        if (id != -1) {
            return getConfigById(id);
        }
        return null;
    }
    
    public boolean hasConfigs() {
        return !configs.isEmpty();
    }
    
    public int getConfigCount() {
        return configs.size();
    }
}
