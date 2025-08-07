package com.example.sshtunnel.models;

public class TunnelConfig {
    private String sshHost;
    private int sshPort;
    private String username;
    private String password;
    private int localPort;
    private String remoteHost;
    private int remotePort;
    private boolean isActive;

    public TunnelConfig() {
        this.sshPort = 22;
        this.localPort = 8080;
        this.remoteHost = "127.0.0.1";
        this.remotePort = 80;
        this.isActive = false;
    }

    public TunnelConfig(String sshHost, int sshPort, String username, String password,
                       int localPort, String remoteHost, int remotePort) {
        this.sshHost = sshHost;
        this.sshPort = sshPort;
        this.username = username;
        this.password = password;
        this.localPort = localPort;
        this.remoteHost = remoteHost;
        this.remotePort = remotePort;
        this.isActive = false;
    }

    // Getters
    public String getSshHost() { return sshHost; }
    public int getSshPort() { return sshPort; }
    public String getUsername() { return username; }
    public String getPassword() { return password; }
    public int getLocalPort() { return localPort; }
    public String getRemoteHost() { return remoteHost; }
    public int getRemotePort() { return remotePort; }
    public boolean isActive() { return isActive; }

    // Setters
    public void setSshHost(String sshHost) { this.sshHost = sshHost; }
    public void setSshPort(int sshPort) { this.sshPort = sshPort; }
    public void setUsername(String username) { this.username = username; }
    public void setPassword(String password) { this.password = password; }
    public void setLocalPort(int localPort) { this.localPort = localPort; }
    public void setRemoteHost(String remoteHost) { this.remoteHost = remoteHost; }
    public void setRemotePort(int remotePort) { this.remotePort = remotePort; }
    public void setActive(boolean active) { this.isActive = active; }

    // Compatibility getters for old code
    public String getServerAddress() { return getSshHost(); }
    public int getServerPort() { return getSshPort(); }

    public boolean isValidSshConfig() {
        return sshHost != null && !sshHost.trim().isEmpty() &&
               username != null && !username.trim().isEmpty() &&
               password != null && !password.trim().isEmpty() &&
               sshPort > 0 && sshPort <= 65535;
    }

    public boolean isValidTunnelConfig() {
        return remoteHost != null && !remoteHost.trim().isEmpty() &&
               localPort > 0 && localPort <= 65535 &&
               remotePort > 0 && remotePort <= 65535;
    }

    @Override
    public String toString() {
        return "TunnelConfig{" +
                "sshHost='" + sshHost + '\'' +
                ", sshPort=" + sshPort +
                ", username='" + username + '\'' +
                ", localPort=" + localPort +
                ", remoteHost='" + remoteHost + '\'' +
                ", remotePort=" + remotePort +
                ", isActive=" + isActive +
                '}';
    }
}