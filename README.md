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
ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -vf "setpts=0,hflip,vflip" http://192.168.1.17:8080/stream.h264
```

### Firefox flags for smooth video (`about:config` page)

```
media.rdd-process.enabled=false
media.ffmpeg.vaapi.enabled=true
javascript.options.mem.max=-1
javascript.options.mem.gc_incremental=true
javascript.options.mem.gc_incremental_slice_ms=5
javascript.options.mem.gc_compacting=false
dom.ipc.processCount=1
```

### Proper 34 FPS `tasks` output

```
I (00:02:04.204) : ┌───────────────────┬──────────┬─────────────┬─────────┬──────────┬───────────┬────────────┬───────┐
I (00:02:04.227) : │ Task              │ Core ID  │ Run Time    │ CPU     │ Priority │ Stack HWM │ State      │ Stack │
I (00:02:04.239) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:04.272) : │ IDLE0             │ 0        │ 965208      │  48.60% │ 0        │ 1240      │ Ready      │ Intr  │
I (00:02:04.284) : │ main              │ 0        │ 256         │   0.01% │ 1        │ 588       │ Blocked    │ Intr  │
I (00:02:04.296) : │ sys_evt           │ 0        │ 0           │   0.00% │ 20       │ 516       │ Blocked    │ Intr  │
I (00:02:04.308) : │ esp_timer         │ 0        │ 0           │   0.00% │ 22       │ 3792      │ Suspended  │ Intr  │
I (00:02:04.319) : │ httpd             │ 0        │ 0           │   0.00% │ 5        │ 37796     │ Blocked    │ Intr  │
I (00:02:04.331) : │ ipc0              │ 0        │ 0           │   0.00% │ 24       │ 728       │ Suspended  │ Intr  │
I (00:02:04.342) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:04.376) : │ IDLE1             │ 1        │ 913529      │  46.00% │ 0        │ 1232      │ Ready      │ Intr  │
I (00:02:04.387) : │ venc_0            │ 1        │ 48185       │   2.43% │ 20       │ 30188     │ Blocked    │ Extr  │
I (00:02:04.399) : │ tiT               │ 1        │ 14394       │   0.72% │ 19       │ 6752      │ Blocked    │ Intr  │
I (00:02:04.410) : │ stream_task       │ 1        │ 7687        │   0.39% │ 17       │ 29876     │ Blocked    │ Intr  │
I (00:02:04.432) : │ ipc1              │ 1        │ 0           │   0.00% │ 24       │ 720       │ Suspended  │ Intr  │
I (00:02:04.444) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:04.467) : │ sdio_read         │ 7fffffff │ 21273       │   1.07% │ 23       │ 3200      │ Blocked    │ Intr  │
I (00:02:04.478) : │ sdio_write        │ 7fffffff │ 7638        │   0.38% │ 23       │ 4336      │ Blocked    │ Intr  │
I (00:02:04.500) : │ console_repl      │ 7fffffff │ 5774        │   0.29% │ 2        │ 2200      │ Running    │ Intr  │
I (00:02:04.512) : │ sdio_process_rx   │ 7fffffff │ 2693        │   0.14% │ 23       │ 3032      │ Blocked    │ Intr  │
I (00:02:04.524) : │ sdio_rx_buf       │ 7fffffff │ 1452        │   0.07% │ 23       │ 1208      │ Blocked    │ Intr  │
I (00:02:04.535) : │ rpc_rx            │ 7fffffff │ 0           │   0.00% │ 23       │ 2440      │ Blocked    │ Intr  │
I (00:02:04.547) : │ rpc_tx            │ 7fffffff │ 0           │   0.00% │ 23       │ 3304      │ Blocked    │ Intr  │
I (00:02:04.558) : │ FtpServer::acce   │ 7fffffff │ 0           │   0.00% │ 5        │ 3300      │ Blocked    │ Intr  │
I (00:02:04.570) : │ Tmr Svc           │ 7fffffff │ 0           │   0.00% │ 1        │ 1720      │ Blocked    │ Intr  │
I (00:02:04.591) : └───────────────────┴──────────┴─────────────┴─────────┴──────────┴───────────┴────────────┴───────┘
```

### OV5647 camera config location -> `managed_components/espressif__esp_cam_sensor/sensors/ov5647/cfg/ov5647_default.json`
