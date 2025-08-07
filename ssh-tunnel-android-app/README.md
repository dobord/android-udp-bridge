# SSH Tunnel Android App

This project is an Android application that utilizes the libssh library to establish an SSH connection to a server and facilitate UDP port forwarding.

## Features

- Connect to a remote server using SSH.
- Forward UDP ports from the local device to the remote server.
- Simple user interface for managing connections.

## Project Structure

- **app/src/main/java/com/example/sshtunnel/**: Contains the main Java classes for the application.
  - **MainActivity.java**: The main activity that initializes the user interface and starts the SSH tunnel service.
  - **SshTunnelService.java**: The service that manages the SSH connection and UDP port forwarding.
  - **models/TunnelConfig.java**: A model class representing the tunnel configuration, including server address and port.

- **app/src/main/jni/**: Contains native code for working with the libssh library.
  - **ssh_tunnel.c**: Implementation of functions for managing the SSH connection and port forwarding.
  - **Android.mk**: Build configuration for the native code using the NDK.

- **app/src/main/res/**: Contains resources for the application.
  - **layout/activity_main.xml**: Layout file for the main activity user interface.
  - **values/strings.xml**: Resource file containing strings used in the application.

- **app/src/main/AndroidManifest.xml**: Manifest file describing the application components and permissions.

- **app/src/cpp/CMakeLists.txt**: Build configuration for C++ code, if added in the future.

- **app/build.gradle**: Build settings for the application module, including dependencies.

- **app/proguard-rules.pro**: ProGuard rules for code minimization and obfuscation.

- **gradle/wrapper/gradle-wrapper.properties**: Configuration for the Gradle Wrapper.

- **build.gradle**: Build settings for the entire project.

- **settings.gradle**: Defines the modules included in the project.

## Installation

1. Clone the repository:
   ```
   git clone <repository-url>
   ```

2. Open the project in Android Studio.

3. Sync the project with Gradle files.

4. Build and run the application on an Android device.

## Usage

1. Enter the server address and port in the application.
2. Click the connect button to establish an SSH connection.
3. Use the application to manage UDP port forwarding as needed.

## License

This project is licensed under the MIT License. See the LICENSE file for details.