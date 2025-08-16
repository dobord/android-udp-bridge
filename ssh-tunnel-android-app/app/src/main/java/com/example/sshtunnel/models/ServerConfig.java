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
    private String remoteBridgeHost;
    private int remoteBridgePort;
    private int localBridgePort;
    private String localBridgeHost;
    private boolean autoReconnect;
    private int connectionTimeout;

    // UDP forwarding settings
    private int localUdpPort;
    private String localUdpHost;
    private String remoteUdpHost;
    private int remoteUdpPort;

    public ServerConfig() {
        this.id = System.currentTimeMillis();
        this.sshPort = 22;
        this.remoteBridgePort = 8080;
        this.localBridgePort = 5060;
        this.localBridgeHost = "127.0.0.1";
        this.connectionTimeout = 30;
        this.autoReconnect = true;
        this.localUdpHost = "127.0.0.1";
        this.remoteUdpHost = "127.0.0.1";
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
        remoteBridgeHost = in.readString();
        remoteBridgePort = in.readInt();
        localBridgePort = in.readInt();
        localBridgeHost = in.readString();
        autoReconnect = in.readByte() != 0;
        connectionTimeout = in.readInt();
        localUdpPort = in.readInt();
        localUdpHost = in.readString();
        remoteUdpHost = in.readString();
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
        dest.writeString(remoteBridgeHost);
        dest.writeInt(remoteBridgePort);
        dest.writeInt(localBridgePort);
        dest.writeString(localBridgeHost);
        dest.writeByte((byte) (autoReconnect ? 1 : 0));
        dest.writeInt(connectionTimeout);
        dest.writeInt(localUdpPort);
        dest.writeString(localUdpHost);
        dest.writeString(remoteUdpHost);
        dest.writeInt(remoteUdpPort);
    }

    // Getters and setters
    public long getId() {
        return id;
    }

    public void setId(long id) {
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getSshHost() {
        return sshHost;
    }

    public void setSshHost(String sshHost) {
        this.sshHost = sshHost;
    }

    public int getSshPort() {
        return sshPort;
    }

    public void setSshPort(int sshPort) {
        this.sshPort = sshPort;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getPassword() {
        return password;
    }

    public void setPassword(String password) {
        this.password = password;
    }

    public boolean isUsePrivateKey() {
        return usePrivateKey;
    }

    public void setUsePrivateKey(boolean usePrivateKey) {
        this.usePrivateKey = usePrivateKey;
    }

    public String getPrivateKeyPath() {
        return privateKeyPath;
    }

    public void setPrivateKeyPath(String privateKeyPath) {
        this.privateKeyPath = privateKeyPath;
    }

    public String getPassphrase() {
        return passphrase;
    }

    public void setPassphrase(String passphrase) {
        this.passphrase = passphrase;
    }

    public String getRemoteBridgeHost() {
        return remoteBridgeHost;
    }

    public void setRemoteBridgeHost(String bridgeHost) {
        this.remoteBridgeHost = bridgeHost;
    }

    public int getRemoteBridgePort() {
        return remoteBridgePort;
    }

    public void setRemoteBridgePort(int bridgePort) {
        this.remoteBridgePort = bridgePort;
    }

    public int getLocalBridgePort() {
        return localBridgePort;
    }

    public void setLocalBridgePort(int localPort) {
        this.localBridgePort = localPort;
    }

    public String getLocalBridgeHost() {
        return localBridgeHost;
    }

    public void setLocalBridgeHost(String localBridgeHost) {
        this.localBridgeHost = localBridgeHost;
    }

    public boolean isAutoReconnect() {
        return autoReconnect;
    }

    public void setAutoReconnect(boolean autoReconnect) {
        this.autoReconnect = autoReconnect;
    }

    public int getConnectionTimeout() {
        return connectionTimeout;
    }

    public void setConnectionTimeout(int connectionTimeout) {
        this.connectionTimeout = connectionTimeout;
    }

    public int getLocalUdpPort() {
        return localUdpPort;
    }

    public void setLocalUdpPort(int localUdpPort) {
        this.localUdpPort = localUdpPort;
    }

    public String getLocalUdpHost() {
        return localUdpHost;
    }

    public void setLocalUdpHost(String localUdpHost) {
        this.localUdpHost = localUdpHost;
    }

    public String getRemoteUdpHost() {
        return remoteUdpHost;
    }

    public void setRemoteUdpHost(String remoteUdpHost) {
        this.remoteUdpHost = remoteUdpHost;
    }

    public int getRemoteUdpPort() {
        return remoteUdpPort;
    }

    public void setRemoteUdpPort(int remoteUdpPort) {
        this.remoteUdpPort = remoteUdpPort;
    }

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
        // Bridge is always enabled, so always validate
        return remoteBridgeHost != null && !remoteBridgeHost.trim().isEmpty() &&
                remoteBridgePort > 0 && remoteBridgePort <= 65535 &&
                localBridgePort > 0 && localBridgePort <= 65535;
    }

    @Override
    public String toString() {
        return getDisplayName();
    }
}
