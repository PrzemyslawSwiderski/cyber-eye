## Board info

**ESP32-P4-NANO-KIT-A Development Board with Wi-Fi 6, Bluetooth 5, Speaker and Camera – Waveshare 29027**

**Description**

The ESP32-P4-NANO-KIT-A is a development kit based on the ESP32-P4 microcontroller, featuring a RISC-V architecture (dual-core + single-core processor) and advanced interfaces for image processing, audio, and communication. The kit includes a Raspberry Pi camera (Model B) with a 5 MP sensor, an 8 Ω 2 W speaker, and a 15-pin FFC ribbon cable for connecting the camera.

The ESP32-P4 supports MIPI-CSI and MIPI-DSI interfaces and enables communication via USB OTG 2.0, Ethernet, and SDIO 3.0. The included ESP32-C6-MINI-1 module provides Wi-Fi 6 and Bluetooth 5/BLE connectivity via the SDIO interface. The platform also offers security features such as Secure Boot, Flash Encryption, TRNG, and privilege separation.

**Features**

* **ESP32-P4NRW32 microcontroller:** RISC-V (2+1 cores), 768 KB L2MEM, 32 MB PSRAM, 16 MB NOR Flash
* **Raspberry Pi Camera (B):** OV5647, 5 MP, manual focus adjustment
* **Connectivity:** ESP32-C6-MINI-1 – Wi-Fi 6, Bluetooth 5/BLE (SDIO interface)
* **Interfaces:** MIPI-CSI, MIPI-DSI, USB 2.0 OTG, 100 Mbps Ethernet, SDIO 3.0 (TF card slot)
* **Peripherals:** microphone, 8 Ω 2 W speaker (MX1.25 2P), RTC, GPIO (2×26 pins)
* **Power supply:** USB Type-C, external 5 V power connector, optional PoE
* **Security features:** Secure Boot, Flash encryption, TRNG, access management
* **Support for ESP-IDF and advanced multimedia interfaces**

**Package contents**

* ESP32-P4-NANO board ×1
* Raspberry Pi Camera (B) 5 MP ×1
* 8 Ω 2 W speaker ×1
* 15-pin FFC ribbon cable ×1

## Potential Issues

### SDKCONFIG

To prevent weird issues with `sdkconfig` file, always remove it on full clean so that proper default are loaded from the `sdkconfig.defaults` and .


### Littlefs registration issue

```
E (00:00:08.485) esp_littlefs: Failed to register Littlefs to "/storage"
[FileSystem/E][10.210]: Failed to initialize LittleFS, unknown error
```

Increase the `CONFIG_VFS_MAX_COUNT` to maximum value:
```
CONFIG_VFS_MAX_COUNT=20
```

### VLC display lag

Use `ffplay` for the minimal latency:

```
ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -vf setpts=0 http://192.168.1.17:8080/stream.h264
```
