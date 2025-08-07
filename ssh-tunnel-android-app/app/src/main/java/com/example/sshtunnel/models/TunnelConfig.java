public class TunnelConfig {
    private String serverAddress;
    private int serverPort;
    private String username;
    private String password;
    private int localPort;
    private int remotePort;

    public TunnelConfig(String serverAddress, int serverPort, String username, String password, int localPort, int remotePort) {
        this.serverAddress = serverAddress;
        this.serverPort = serverPort;
        this.username = username;
        this.password = password;
        this.localPort = localPort;
        this.remotePort = remotePort;
    }

    public String getServerAddress() {
        return serverAddress;
    }

    public int getServerPort() {
        return serverPort;
    }

    public String getUsername() {
        return username;
    }

    public String getPassword() {
        return password;
    }

    public int getLocalPort() {
        return localPort;
    }

    public int getRemotePort() {
        return remotePort;
    }
}