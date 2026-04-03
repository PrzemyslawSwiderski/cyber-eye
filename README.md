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

### Proper 43 FPS `tasks` output for 

```
I (00:00:07.631) VID_PIPE_NEGO: Success to negotiate 0 format:o_uyy_e_vyy 1280x960 45fps
cyber-eye>  ta[VideoStream/I][132.574]: Streaming FPS: 43.2, total frames=266, dropped=0
cyber-eye>  tasks
[VideoStream/I][135.584]: Streaming FPS: 43.2, total frames=396, dropped=0
I (00:02:15.133) : ┌───────────────────┬──────────┬─────────────┬─────────┬──────────┬───────────┬────────────┬───────┐
I (00:02:15.156) : │ Task              │ Core ID  │ Run Time    │ CPU     │ Priority │ Stack HWM │ State      │ Stack │
I (00:02:15.168) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:15.205) : │ IDLE0             │ 0        │ 679611      │  34.14% │ 0        │ 1224      │ Ready      │ Intr  │
I (00:02:15.216) : │ MUSIC_PLAYER      │ 0        │ 276202      │  13.87% │ 21       │ 1640      │ Blocked    │ Intr  │
I (00:02:15.235) : │ main              │ 0        │ 259         │   0.01% │ 1        │ 652       │ Blocked    │ Intr  │
I (00:02:15.247) : │ music_ctrl        │ 0        │ 86          │   0.00% │ 21       │ 1988      │ Blocked    │ Intr  │
I (00:02:15.259) : │ ipc0              │ 0        │ 0           │   0.00% │ 24       │ 596       │ Suspended  │ Intr  │
I (00:02:15.271) : │ sys_evt           │ 0        │ 0           │   0.00% │ 20       │ 680       │ Blocked    │ Intr  │
I (00:02:15.284) : │ esp_timer         │ 0        │ 0           │   0.00% │ 22       │ 3792      │ Suspended  │ Intr  │
I (00:02:15.295) : │ httpd             │ 0        │ 0           │   0.00% │ 5        │ 37584     │ Blocked    │ Intr  │
I (00:02:15.310) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:15.350) : │ IDLE1             │ 1        │ 915125      │  45.97% │ 0        │ 1216      │ Ready      │ Intr  │
I (00:02:15.364) : │ venc_0            │ 1        │ 39864       │   2.00% │ 14       │ 13804     │ Blocked    │ Extr  │
I (00:02:15.376) : │ tiT               │ 1        │ 20734       │   1.04% │ 15       │ 6752      │ Ready      │ Intr  │
I (00:02:15.390) : │ stream_task       │ 1        │ 9958        │   0.50% │ 12       │ 5320      │ Blocked    │ Intr  │
I (00:02:15.402) : │ ipc1              │ 1        │ 0           │   0.00% │ 24       │ 720       │ Suspended  │ Intr  │
I (00:02:15.415) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (00:02:15.445) : │ sdio_read         │ 7fffffff │ 31246       │   1.57% │ 23       │ 3200      │ Blocked    │ Intr  │
I (00:02:15.470) : │ sdio_write        │ 7fffffff │ 11345       │   0.57% │ 23       │ 4336      │ Blocked    │ Intr  │
I (00:02:15.481) : │ sdio_process_rx   │ 7fffffff │ 4071        │   0.20% │ 23       │ 3032      │ Blocked    │ Intr  │
I (00:02:15.494) : │ console_repl      │ 7fffffff │ 2694        │   0.14% │ 2        │ 2204      │ Running    │ Intr  │
I (00:02:15.505) : │ sdio_rx_buf       │ 7fffffff │ 2139        │   0.11% │ 23       │ 1212      │ Blocked    │ Intr  │
I (00:02:15.519) : │ rpc_rx            │ 7fffffff │ 0           │   0.00% │ 23       │ 2460      │ Blocked    │ Intr  │
I (00:02:15.530) : │ rpc_tx            │ 7fffffff │ 0           │   0.00% │ 23       │ 3288      │ Blocked    │ Intr  │
I (00:02:15.544) : │ FtpServer::acce   │ 7fffffff │ 0           │   0.00% │ 5        │ 3304      │ Blocked    │ Intr  │
I (00:02:15.556) : │ Tmr Svc           │ 7fffffff │ 0           │   0.00% │ 1        │ 1720      │ Blocked    │ Intr  │
I (00:02:15.576) : └───────────────────┴──────────┴─────────────┴─────────┴──────────┴───────────┴────────────┴───────┘
```

### OV5647 camera config location -> `managed_components/espressif__esp_cam_sensor/sensors/ov5647/cfg/ov5647_default.json`
