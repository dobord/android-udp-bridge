package com.example.sshtunnel.models;

import android.os.Parcel;
import android.os.Parcelable;

public class ServerConfig implements Parcelable {
    private long id;
    private String name;
    private String sshHost;
    private int sshPort;
    private String username;
    private String password;
    private boolean usePrivateKey;
    private String privateKeyPath;
    private String passphrase;
    
    // Bridge settings
    private boolean bridgeEnabled;
    private String bridgeHost;
    private int bridgePort;
    private int localPort;
    private boolean autoReconnect;
    private int connectionTimeout;
    
    // Advanced UDP forwarding settings
    private int localUdpPort;
    private String remoteHost;
    private int remoteUdpPort;

    public ServerConfig() {
        this.id = System.currentTimeMillis();
        this.sshPort = 22;
        this.bridgePort = 8080;
        this.localPort = 5060;
        this.connectionTimeout = 30;
        this.autoReconnect = true;
        this.remoteHost = "127.0.0.1";
        this.localUdpPort = 0;
        this.remoteUdpPort = 0;
    }

    public ServerConfig(String name, String sshHost, int sshPort, String username, String password) {
        this();
        this.name = name;
        this.sshHost = sshHost;
        this.sshPort = sshPort;
        this.username = username;
        this.password = password;
    }

    // Parcelable implementation
    protected ServerConfig(Parcel in) {
        id = in.readLong();
        name = in.readString();
        sshHost = in.readString();
        sshPort = in.readInt();
        username = in.readString();
        password = in.readString();
        usePrivateKey = in.readByte() != 0;
        privateKeyPath = in.readString();
        passphrase = in.readString();
        bridgeEnabled = in.readByte() != 0;
        bridgeHost = in.readString();
        bridgePort = in.readInt();
        localPort = in.readInt();
        autoReconnect = in.readByte() != 0;
        connectionTimeout = in.readInt();
        localUdpPort = in.readInt();
        remoteHost = in.readString();
        remoteUdpPort = in.readInt();
    }

    public static final Creator<ServerConfig> CREATOR = new Creator<ServerConfig>() {
        @Override
        public ServerConfig createFromParcel(Parcel in) {
            return new ServerConfig(in);
        }

        @Override
        public ServerConfig[] newArray(int size) {
            return new ServerConfig[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeLong(id);
        dest.writeString(name);
        dest.writeString(sshHost);
        dest.writeInt(sshPort);
        dest.writeString(username);
        dest.writeString(password);
        dest.writeByte((byte) (usePrivateKey ? 1 : 0));
        dest.writeString(privateKeyPath);
        dest.writeString(passphrase);
        dest.writeByte((byte) (bridgeEnabled ? 1 : 0));
        dest.writeString(bridgeHost);
        dest.writeInt(bridgePort);
        dest.writeInt(localPort);
        dest.writeByte((byte) (autoReconnect ? 1 : 0));
        dest.writeInt(connectionTimeout);
        dest.writeInt(localUdpPort);
        dest.writeString(remoteHost);
        dest.writeInt(remoteUdpPort);
    }

    // Getters and setters
    public long getId() { return id; }
    public void setId(long id) { this.id = id; }

    public String getName() { return name; }
    public void setName(String name) { this.name = name; }

    public String getSshHost() { return sshHost; }
    public void setSshHost(String sshHost) { this.sshHost = sshHost; }

    public int getSshPort() { return sshPort; }
    public void setSshPort(int sshPort) { this.sshPort = sshPort; }

    public String getUsername() { return username; }
    public void setUsername(String username) { this.username = username; }

    public String getPassword() { return password; }
    public void setPassword(String password) { this.password = password; }

    public boolean isUsePrivateKey() { return usePrivateKey; }
    public void setUsePrivateKey(boolean usePrivateKey) { this.usePrivateKey = usePrivateKey; }

    public String getPrivateKeyPath() { return privateKeyPath; }
    public void setPrivateKeyPath(String privateKeyPath) { this.privateKeyPath = privateKeyPath; }

    public String getPassphrase() { return passphrase; }
    public void setPassphrase(String passphrase) { this.passphrase = passphrase; }

    public boolean isBridgeEnabled() { return bridgeEnabled; }
    public void setBridgeEnabled(boolean bridgeEnabled) { this.bridgeEnabled = bridgeEnabled; }

    public String getBridgeHost() { return bridgeHost; }
    public void setBridgeHost(String bridgeHost) { this.bridgeHost = bridgeHost; }

    public int getBridgePort() { return bridgePort; }
    public void setBridgePort(int bridgePort) { this.bridgePort = bridgePort; }

    public int getLocalPort() { return localPort; }
    public void setLocalPort(int localPort) { this.localPort = localPort; }

    public boolean isAutoReconnect() { return autoReconnect; }
    public void setAutoReconnect(boolean autoReconnect) { this.autoReconnect = autoReconnect; }

    public int getConnectionTimeout() { return connectionTimeout; }
    public void setConnectionTimeout(int connectionTimeout) { this.connectionTimeout = connectionTimeout; }

    public int getLocalUdpPort() { return localUdpPort; }
    public void setLocalUdpPort(int localUdpPort) { this.localUdpPort = localUdpPort; }

    public String getRemoteHost() { return remoteHost; }
    public void setRemoteHost(String remoteHost) { this.remoteHost = remoteHost; }

    public int getRemoteUdpPort() { return remoteUdpPort; }
    public void setRemoteUdpPort(int remoteUdpPort) { this.remoteUdpPort = remoteUdpPort; }

    // Utility methods
    public String getDisplayName() {
        if (name != null && !name.trim().isEmpty()) {
            return name;
        }
        return sshHost + ":" + sshPort;
    }

    public boolean isValidSshConfig() {
        return sshHost != null && !sshHost.trim().isEmpty() &&
               username != null && !username.trim().isEmpty() &&
               sshPort > 0 && sshPort <= 65535 &&
               ((usePrivateKey && privateKeyPath != null && !privateKeyPath.trim().isEmpty()) ||
                (!usePrivateKey && password != null && !password.trim().isEmpty()));
    }

    public boolean isValidBridgeConfig() {
        if (!bridgeEnabled) return true;
        return bridgeHost != null && !bridgeHost.trim().isEmpty() &&
               bridgePort > 0 && bridgePort <= 65535 &&
               localPort > 0 && localPort <= 65535;
    }

    @Override
    public String toString() {
        return getDisplayName();
    }
}
