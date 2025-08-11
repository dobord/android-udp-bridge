package com.example.sshtunnel.models;

import android.content.Context;
import android.content.SharedPreferences;
import com.google.gson.Gson;
import com.google.gson.reflect.TypeToken;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;

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
        String configsJson = gson.toJson(configs);
        prefs.edit().putString(KEY_CONFIGS, configsJson).apply();
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
        if (config.getId() == 0) {
            config.setId(System.currentTimeMillis());
        }
        configs.add(config);
        saveConfigs();
    }
    
    public void updateConfig(ServerConfig config) {
        for (int i = 0; i < configs.size(); i++) {
            if (configs.get(i).getId() == config.getId()) {
                configs.set(i, config);
                saveConfigs();
                return;
            }
        }
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
