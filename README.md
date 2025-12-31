# Mirror

**Mirror** is a high-performance, low-latency screen and camera mirroring application. It uses a C++ WebSocket server for signaling and WebRTC for peer-to-peer media streaming.

## Features
- **Low Latency**: Direct P2P connection via WebRTC.
- **Cross-Platform**: Works on any device with a modern web browser (Laptop, Mobile, Smart TV).
- **Simple Setup**: Scan a QR code to connect.
- **Multiple Sources**: Share your screen or camera.

## Prerequisites
Before you begin, ensure you have the following installed:
- **C++17** compiler (GCC/Clang/MSVC)
- **CMake** (3.10 or higher)
- **Boost Libraries** (System, Thread)
- **nlohmann_json** (JSON for Modern C++)

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake libboost-all-dev nlohmann-json3-dev
```

## Build Instructions

1.  Clone the repository and navigate to the project directory.
2.  Create a build directory:
    ```bash
    mkdir build && cd build
    ```
3.  Configure with CMake:
    ```bash
    cmake ..
    ```
4.  Build the server:
    ```bash
    make
    ```

## Usage

### 1. Start the Server
Run the compiled server executable. It will listen on port `8080` by default.
```bash
./mirror_server
```
You should see: `Server listening on 0.0.0.0:8080 (HTTP)`

### 2. Connect the Receiver (TV / Monitor)
Open a web browser on the device you want to **receive** the video (e.g., Smart TV, Laptop):
- Navigate to: `http://<YOUR_COMPUTER_IP>:8080/`
- You will see a **QR Code** and a session link.
- The status will show "Connecting... | WS Connected".

> **Note:** Replace `<YOUR_COMPUTER_IP>` with your machine's local IP address (e.g., `192.168.1.5`). Do not use `localhost` if connecting from a different device.

### 3. Connect the Sender (Phone / Laptop)
On the device you want to **share** from:
- **Option A:** Scan the QR code displayed on the Receiver.
- **Option B:** Open the link displayed on the Receiver manually.

The sender interface will load.

### 4. Start Mirroring
- Click **1. Connect Server** to establish the WebSocket signaling link.
- Click **2. Share Screen** or **2. Share Camera**.
- Grant the necessary permissions.
- The video should appear instantly on the Receiver.

## Troubleshooting

- **"Waiting for ICE..." / Video not loading:**
    - Ensure both devices are on the **same Wi-Fi network**.
    - If you are using a VPN, **disable it**.
    - Check if your firewall is blocking port `8080`.
- **"WS not connected":**
    - Ensure the server is running.
    - Check if the IP address is correct.

## Architecture
- **Server**: C++ (Boost.Beast) handles HTTP serving and WebSocket signaling (SDP/ICE exchange).
- **Client**: HTML5/JavaScript uses the WebRTC API for media capture and transmission.
