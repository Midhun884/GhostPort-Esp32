# 👻 GhostPort-ESP32 — ESP32 Network Tunneling Client

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-Compatible-blue.svg" alt="ESP32 Compatible">
  <img src="https://img.shields.io/badge/Project-Educational-orange.svg" alt="Educational Project">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
</p>

<p align="center">
  <b>A low-cost ESP32 client for learning secure networking, tunneling concepts, and ethical hacking and penetration testing in controlled environments</b>
</p>

---
<img width="653" height="460" alt="Screenshot 2026-07-16 231046" src="https://github.com/user-attachments/assets/9c5e0886-e2b4-4935-b38b-9f925a915d06" />

# ⚠️ Educational Use Disclaimer

**GhostPort-ESP32 is intended only for educational purposes, security research, and authorized testing.**

This project is designed to help developers and security enthusiasts understand:

* Embedded networking concepts
* TCP communication
* Remote-access architectures
* IoT security research
* Network tunneling principles

Do not use this project to access systems, networks, services, or devices without proper authorization.

Always:

* Test only in environments you own or have permission to access
* Follow responsible security practices
* Respect privacy and legal requirements
* Avoid exposing sensitive services directly to the internet

---

# 📖 About GhostPort-ESP32

GhostPort-ESP32 is an ESP32-based networking client that connects to a central tunneling server and provides controlled remote access to authorized services on a local network.

The ESP32 acts as a lightweight communication bridge between a local network service and a remotely hosted GhostPort server.

```text
┌────────────────┐
│ Local Service  │
│  Authorized    │
└───────┬────────┘
        │
        ▼
┌────────────────┐
│     ESP32      │
│   GhostPort    │
│     Client     │
└───────┬────────┘
        │
        ▼
┌────────────────┐
│   GhostPort    │
│     Server     │
│  ShadowPort    │
└───────┬────────┘
        │
        ▼
┌────────────────┐
│  Remote User   │
│  Authorized    │
└────────────────┘
```

---

# 🔗 Server Component

GhostPort-ESP32 uses the ShadowPort server component i created earlier.

**Original server repository:**

https://github.com/Midhun884/shadowport.git

The ShadowPort server provides the backend tunnel-management and communication layer.

Refer to the original repository for:

* Server installation
* Backend configuration
* Server-side operation
* Tunnel management
* Protocol details

---

# ✨ Features

## ESP32 Client

* ESP32 Wi-Fi connectivity
* Lightweight embedded tunneling client
* Remote server communication
* Web-based configuration interface
* Persistent configuration storage
* Multiple proxy configuration support
* Automatic reconnection support
* Local service forwarding

## Supported Configuration

GhostPort-ESP32 can be configured with:

* Wi-Fi network name
* Wi-Fi password
* Server address
* Server port
* Authentication token
* Local service address
* Local service port
* Remote access port
* Tunnel mappings

---

# 🧰 Hardware Requirements

| Component               | Description                         |
| ----------------------- | ----------------------------------- |
| ESP32 Development Board | Main controller                     |
| USB Cable               | Used for programming and power      |
| Computer                | Required for uploading the firmware |
| Optional Power Bank     | Useful for portable testing         |

Estimated hardware cost:

```text
ESP32 Development Board: Approximately $5 USD
```

---

# 💻 Software Requirements

Install the following software before uploading the firmware:

* Arduino IDE
* ESP32 Board Support Package
* ArduinoJson Library
* USB driver for your ESP32 board, when required

---

# 🚀 Getting Started

## 1. Download or Clone the Repository

Clone the repository using Git:

```bash
git clone https://github.com/<yourusername>/ghostport-esp32.git
```

Open the downloaded folder:

```bash
cd ghostport-esp32
```

You may also download the project as a ZIP file from GitHub and extract it on your computer.

---

## 2. Install Arduino IDE

Download and install the latest stable version of Arduino IDE.

After installation, open Arduino IDE.

---

## 3. Install ESP32 Board Support

In Arduino IDE:

1. Open:

   ```text
   File → Preferences
   ```

2. Add the following URL under **Additional Boards Manager URLs**:

   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

3. Click **OK**.

4. Open:

   ```text
   Tools → Board → Boards Manager
   ```

5. Search for:

   ```text
   ESP32
   ```

6. Install the package published by Espressif Systems.

---

## 4. Install ArduinoJson

In Arduino IDE:

1. Open:

   ```text
   Tools → Manage Libraries
   ```

2. Search for:

   ```text
   ArduinoJson
   ```

3. Install the latest compatible version of the ArduinoJson library.

---

## 5. Open the Firmware

Open the following firmware file in Arduino IDE:

```text
ghostport_esp32.ino
```

Ensure that the `.ino` file and any related source files are stored inside the same project folder.

---

## 6. Configure Default Settings

Update the default configuration values in the firmware when required:

```cpp
static const char* DEFAULT_WIFI_SSID = "YourWiFi";
static const char* DEFAULT_WIFI_PASS = "YourPassword";

static const char* DEFAULT_SERVER_HOST = "your-server-address";

static const uint16_t DEFAULT_SERVER_PORT = 7000;

static const char* DEFAULT_TOKEN = "YourSecureToken";
```

For safer public releases, avoid uploading real Wi-Fi passwords, production server addresses, or authentication tokens to GitHub.

Use placeholder values in the public source code.

---

# 📤 Upload Firmware Using Arduino IDE

The firmware must be compiled and uploaded using Arduino IDE.

## Upload Steps

1. Connect the ESP32 development board to your computer using a USB cable.

2. Open Arduino IDE.

3. Open the firmware file:

   ```text
   ghostport_esp32.ino
   ```

4. Select the ESP32 board:

   ```text
   Tools → Board → ESP32 Arduino → ESP32 Dev Module
   ```

5. Select the USB port connected to the ESP32:

   ```text
   Tools → Port
   ```

6. Configure the upload speed when required:

   ```text
   Tools → Upload Speed → 115200
   ```

7. Click the **Verify** button to compile the firmware.

8. After successful compilation, click the **Upload** button.

9. Wait until Arduino IDE displays a successful upload message.

Some ESP32 boards may require you to hold the **BOOT** button when the upload process begins. Release the button when the firmware starts uploading.

---

# 🖥️ Serial Monitor

After uploading the firmware, open the Serial Monitor to view the ESP32 logs.

In Arduino IDE, open:

```text
Tools → Serial Monitor
```

Set the baud rate to:

```text
115200
```

The Serial Monitor can display:

* ESP32 startup status
* Wi-Fi connection status
* Assigned IP address
* Server connection status
* Configuration mode status
* Tunnel connection logs

---

# ⚙️ Web Configuration Mode

GhostPort-ESP32 includes a web-based configuration interface.

## Configuration Steps

1. Power on or restart the ESP32.

2. Open the Wi-Fi settings on your phone or computer.

3. Connect to the ESP32 configuration network:

   ```text
   GhostPort-Setup
   ```

4. Open a web browser.

5. Visit:

   ```text
   http://192.168.4.1:8088/
   ```

6. Enter the required configuration:

   * Wi-Fi network
   * Wi-Fi password
   * GhostPort server address
   * GhostPort server port
   * Authentication token
   * Local service details
   * Tunnel mappings

7. Save the configuration.

8. Restart the ESP32.

The ESP32 will then attempt to connect to the configured Wi-Fi network and GhostPort server.

---

# 🔧 Example Configuration

A basic GhostPort tunnel works as follows:

```text
Remote Access Port
        │
        ▼
GhostPort Server
        │
        ▼
ESP32 Tunnel Client
        │
        ▼
Authorized Local Service
```

Example service mappings:

```text
Remote Port 8080 → Local Web Service
Remote Port 2222 → Local SSH Service
```

Example local services may include:

* Web dashboards
* Development servers
* Home-lab services
* Test APIs
* Authorized SSH services
* IoT administration interfaces

Only services you own or are authorized to access should be configured.

---

# 📊 Performance Notes

Performance depends on several factors:

* Wi-Fi signal quality
* Internet connection stability
* Network latency
* ESP32 model
* Tunnel protocol overhead
* Number of active tunnels
* Type of forwarded traffic
* GhostPort server location

Typical ESP32 limitations include:

| Resource    | Details                               |
| ----------- | ------------------------------------- |
| Wi-Fi       | 2.4 GHz                               |
| Processor   | Dual-core Xtensa, depending on model  |
| RAM         | Limited embedded memory               |
| Storage     | Depends on onboard flash              |
| Throughput  | Suitable for lightweight services     |
| Connections | Limited compared with desktop clients |

GhostPort-ESP32 is intended for learning, testing, demonstrations, and lightweight networking tasks.

It is not intended to replace high-performance routers, VPN gateways, or production-grade tunneling appliances.

---

# 🧪 Educational Applications

GhostPort-ESP32 may be used for:

* IoT networking experiments
* Embedded communication research
* Home-lab environments
* Network architecture demonstrations
* Security-awareness training
* TCP socket programming experiments
* Remote-device management demonstrations
* ESP32 web-configuration projects
* Authentication-token experiments
* Controlled tunnel testing

---

# 🔐 Security Recommendations

For safer deployments:

* Use strong and unique authentication tokens
* Do not hard-code production credentials
* Protect the GhostPort server with firewall rules
* Keep ESP32 firmware updated
* Keep server dependencies updated
* Limit the number of exposed services
* Avoid exposing administrative services unnecessarily
* Monitor incoming and outgoing connections
* Rotate authentication tokens periodically
* Use encrypted communication wherever supported
* Disable configuration mode after setup when possible
* Use only networks and systems you are authorized to access

The ESP32 configuration access point should not remain permanently enabled unless required.

---

# 🤝 Contributing

Contributions are welcome and appreciated.

You may contribute by:

* Reporting bugs
* Improving documentation
* Submitting feature requests
* Adding usage examples
* Improving firmware stability
* Enhancing configuration management
* Adding support for additional ESP32 boards
* Sharing authorized educational use cases

## Contribution Process

1. Fork the repository.

2. Create a feature branch:

   ```bash
   git checkout -b feature/my-improvement
   ```

3. Make the required changes.

4. Commit your changes:

   ```bash
   git commit -m "Improve ESP32 configuration handling"
   ```

5. Push the branch:

   ```bash
   git push origin feature/my-improvement
   ```

6. Open a pull request.

Please ensure that contributions follow responsible security and ethical-testing practices.

---

# 🌟 Acknowledgements

Special thanks to:

* The ESP32 developer community
* Open-source contributors
* Security researchers promoting responsible research
* Developers working on embedded networking
* The original ShadowPort server project

Original ShadowPort repository:

https://github.com/Midhun884/shadowport.git

---

# 📜 License

This project is released under the MIT License.

Refer to the following file for complete license information:

```text
LICENSE
```

---

# 📌 Project Status

GhostPort-ESP32 is an educational open-source project.

Planned improvements may include:

* Better configuration management
* Additional ESP32 board support
* Improved documentation
* Additional tunnel examples
* Enhanced connection stability
* Improved reconnection handling
* Secure credential storage
* Improved configuration validation
* Optional encrypted tunnel communication
* Better connection-status reporting

---

<p align="center">
  👻 <b>GhostPort-ESP32</b><br>
  Learn networking. Build responsibly. Secure systems better.
</p>
