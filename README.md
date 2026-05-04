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

### Video stream perfomance tests

Use `ffplay` for the minimal latency:

```
ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -vf "setpts=0,hflip,vflip" http://192.168.1.17:8080/stream.h264
```

### Latency measure

#### IPS:

##### NJU network
- Latife -> 192.168.1.12
- ESP -> 192.168.1.17

##### ESP AP mode
- Latife -> 192.168.4.2
- ESP -> 192.168.4.1

#### Ping
ping 192.168.1.17

#### Simulate standard RTP/UDP H264 packet (most realistic)
ping -s 1400 -c 100 -i 0.2 192.168.1.17

#### Small packets - if these are clean, it's definitely buffer bloat
ping -s 32 -c 100 -i 0.2 192.168.1.17

#### Iperf

- Run the demo as station mode and join the target AP
  - `sta_connect <ssid> <password>`
  - NOTE: the dut is started in station mode by default. If you want to use the dut as softap, please set wifi mode first:
    - `wifi_mode ap`
    - `ap_set <dut_ap_ssid> <dut_ap_password>`

- Run iperf as server on AP side (TCP)
  - `iperf -s -i 3`

- Run iperf as client on ESP side (TCP)
  - `iperf -c 192.168.1.12 -i 3 -t 60`


- Run iperf as server on AP side (UDP)
  - `iperf -u -s -i 1`

- Run iperf as client on ESP side (UDP)
  - `iperf -u -c 192.168.1.12 -i 1 -t 60`


- Run iperf as server on ESP side (UDP)
  - `iperf -u -s -i 1`

- Run iperf as client on AP side (UDP)
  - `iperf -u -c 192.168.1.17 -b 6M -l 1400 -i 1 -t 60 --verbose`
  - `iperf -u -c 192.168.1.12 -b 6M -l 1400 -i 1 -t 60`

```
# 3 Mbps stream (480p/720p HD simulation)
iperf -u -c <PC_IP_ADDRESS> -b 3M -l 1400 -i 1 -t 60

# 6 Mbps stream (720p/1080p HD simulation)
iperf -u -c <PC_IP_ADDRESS> -b 6M -l 1400 -i 1 -t 60

# 10 Mbps stream (high-bitrate 1080p simulation)
iperf -u -c <PC_IP_ADDRESS> -b 10M -l 1400 -i 1 -t 60
```

### UDP testing

```
# Start stream
echo -n "start" | nc -u 192.168.1.17 3334

# Stop stream
echo -n "stop" | nc -u 192.168.1.17 3334

# Status
echo -n "status" | nc -u 192.168.1.17 3334

nc -u 192.168.1.17 3333

socat UDP-RECV:3333 STDOUT | ffplay -

socat UDP-RECV:3333 STDOUT | ffplay -f h264 -
```

RTP:
```
# Start the streamer on ESP32 (send 'start' command)
echo "start" | nc -u 192.168.1.17 3334

# Play with VLC (on Ubuntu/Linux)
vlc udp://@:3333

# Or from command line with network cache
vlc udp://@:3333 --network-caching=300
vlc rtp://@:3333 --network-caching=300

# Or using ffplay
ffplay udp://0.0.0.0:3333


# Step 1: Check if data is arriving
nc -ul 3333 -v

# Step 2: Check if it's valid RTP
ffprobe -v debug -i udp://@:3333
ffprobe -v debug -i rtp://@:3333

# Step 3: Try playing with minimal logging
ffplay -loglevel debug -stats udp://@:3333 2>&1 | grep -E "(error|Error|frame|pts)"

# Step 4: Check codec compatibility
ffprobe -show_streams udp://@:3333 2>&1 | grep codec

# Step 5: Force RTP depacketization
ffplay -f rtp_mpegts udp://@:3333


```

```
# Save raw UDP data to file
timeout 10 nc -ul 3333 > received.h264

# Check if file has data
ls -lh received.h264

# Analyze the file
ffprobe received.h264
hexdump -C received.h264 | head -20
```

### SDP PLAY
```
ffplay -fflags nobuffer \
       -flags low_delay \
       -framedrop \
       -max_delay 500000 \
       -protocol_whitelist "file,udp,rtp" \
        stream.sdp
```


### Video Devices info

```
video:> v4l2-ctl -d /dev/video11 --all
Driver Info:
        Driver name      : H.264
        Card type        : H.264
        Bus info         : esp32p4:H.264
        Driver version   : 2.1.0
        Capabilities     : 0x84208000
                Video M2M
                Streaming
                Extended Pix Format
                Device Capabilities
        Device Caps      : 0x4208000
                Video M2M
                Streaming
                Extended Pix Format
Format Video Capture:
        Width/Height     : 64/64
        Pixel Format     : H264

Controls

            H264 I-Frame Period 0x00990a66 (int)        : min=1 max=120 step=1 default=30
          H264 Minimum QP Value 0x00990a61 (int)        : min=0 max=51 step=1 default=25
          H264 Maximum QP Value 0x00990a62 (int)        : min=0 max=51 step=1 default=26
                  Video Bitrate 0x009909cf (int)        : min=25000 max=25000000 step=25000 default=10000000

Controls 2

video:> v4l2-ctl -d /dev/video0 --all
Driver Info:
        Driver name      : MIPI-CSI
        Card type        : MIPI-CSI
        Bus info         : esp32p4:MIPI-CSI
        Driver version   : 2.1.0
        Capabilities     : 0x84200001
                Video Capture
                Streaming
                Extended Pix Format
                Device Capabilities
        Device Caps      : 0x4200001
                Video Capture
                Streaming
                Extended Pix Format
Format Video Capture:
        Width/Height     : 800/800
        Pixel Format     : RGBP

Controls

                       Exposure 0x00980911 (int)        : min=2 max=235 step=1 default=80
                  Vertical Flip 0x00980915 (int)        : min=0 max=1 step=1 default=0
                Horizontal Flip 0x00980914 (int)        : min=0 max=1 step=1 default=0

Controls 2
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


### SLAVE FIRMWARE UPDATE https://github.com/espressif/esp-hosted-mcu/blob/main/examples/host_performs_slave_ota/README.md


### GREP CMD: `grep -r "sdio_read\"" managed_components/espressif__esp_hosted/`


## Launching Cyber-Eye console from https://cyber-eye.przemyslaw-swiderski7.workers.dev/

Ubuntu terminal:
```bash
firefox -P dev --no-remote
```

`security.mixed_content.block_active_content` set to `false`.
