## CP2102 UART connection

### Find your CP2102 Device Name

`sudo dmesg | grep tty`

### Connection

`picocom -b 115200 /dev/ttyUSB0`

Initial Login: root
Password: luckfox

### How to Exit Picocom

When you are done and want to close the connection to return to your normal Ubuntu terminal, press the keyboard shortcut sequence:
Ctrl + A, followed immediately by Ctrl + X.


## USB-C connection

## Static IP Rule mapping

`sudo nano /etc/udev/rules.d/70-luckfox.rules`
add:

`SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="2207", ATTRS{idProduct}=="0019", NAME="luckfox0"`

`sudo chmod 644 /etc/udev/rules.d/70-luckfox.rules`

`nmcli con add type ethernet ifname luckfox0 con-name luckfox ip4 172.32.0.98/24`
`nmcli con up luckfox`

### Static IP assignment

`sudo mkdir /etc/network/interfaces.d`
`sudo nano /etc/network/interfaces.d/luckfox`

add
```
allow-hotplug luckfox0
iface luckfox0 inet static
    address 172.32.0.98/24
    netmask 255.255.0.0
```


### Result
`ping 172.32.0.93`
This way the IP is assigned automatically on every reconnect without needing to run ip addr add manually.


### USB mode

`cat /sys/devices/platform/ff3e0000.usb2-phy/otg_mode`

## Connect by telnet

`telnet 172.32.0.93`

Initial Login: root
Password: luckfox

## Find any files

`find / -name "RkLunch.sh" 2>/dev/null`
`find / -name "*.ko" 2>/dev/null`

## Video stream

List video controls:
```sh
v4l2-ctl -d /dev/v4l-subdev2 --list-ctrls
# exposure -> min=1 max=1352 step=1 default=128
# analogue_gain -> min=128 max=99614 step=1 default=128
v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl=exposure=600,analogue_gain=7000,horizontal_flip=1,vertical_flip=1
```

Test and save to MP4 file (all vide modules have to be installed before)
```sh
# v4l2-ctl --device=/dev/video11 --set-fmt-video=width=640,height=480,pixelformat=NV12 --stream-mmap --stream-to=video.yuv --stream-count=10
# ffmpeg -f rawvideo -pix_fmt nv12 -s 640x480 -i video.yuv -c:v mpeg4 -pix_fmt yuv420p -r 10 output.mp4

mkfifo /tmp/venc.h264

python /root/app.py

simple_vi_bind_venc -I 0 -w 1920 -h 1080 -e h264 -o /tmp/venc.h264

```

```sh
gst-launch-1.0 v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
  mpph264enc ! h264parse ! rtph264pay config-interval=1 pt=96 ! \
  udpsink host=192.168.1.12 port=5000
```


## Video view

Stream from Luckfox:
```sh
/oem/usr/bin/RkLunch.sh

ffmpeg -f v4l2 -framerate 30 -video_size 1280x720 -i /dev/video0 -c:v h264 -f rtp "rtp://192.168.1.12:5600"
```


USB network:
`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport tcp rtsp://172.32.0.93:554/live/0`

Nju network:
`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport udp rtsp://192.168.1.15:554/live/0`
`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport udp rtsp://192.168.1.15:3893/live/0`


## Adjust build root config

```sh
./build.sh buildrootconfig
```

## Build image


Build with docker:
`sudo docker pull luckfoxtech/luckfox_pico:1.0`

Start an instance with pseudo-TTY:
```sh
sudo docker container rm luckfox
sudo docker run -it --ipc=host --privileged --name luckfox \
      -v /home/pswidersk/REPOS/luckfox-pico:/home/ \
      -v /home/pswidersk/REPOS/cyber-eye/luckfox/overlay:/home/project/cfg/BoardConfig_IPC/overlay/custom-overlay \
      luckfoxtech/luckfox_pico:1.0 /bin/bash
```

If the container already exists, you can restart it using the following command:

`sudo docker start -ai luckfox`

or remove with:

`sudo docker container rm luckfox`

Build SDK:
```sh
cd /home
./build.sh lunch
./build.sh 
```

Rebuild packages only:
```sh
cd /home
./build.sh clean rootfs
./build.sh rootfs
```


## Run the build process to generate a rootfs.img that includes the overlay content.`
`./build.sh firmware`

Flashing:

https://wiki.luckfox.com/Luckfox-Pico-Plus-Mini/Flash-image#51-flashing-image-to-spi-nand-flash


## Cross compile BL-M8812EU2 [driver](https://github.com/svpcom/rtl8812eu/)

Driver code was placed in `/home/sysdrv/drv_ko/wifi/rtl8812eu`
```sh
./build.sh driver
```


Load the build driver inside Luckfox chip
```sh
insmod cfg80211.ko
insmod 8812eu.ko
```

Save new network config:
```sh
network={
     ssid="myssid"
     psk="12345678"
}
```
in `/etc/wpa_supplicant.conf` file.

connect to network:
```sh
wpa_supplicant -D nl80211 -c /etc/wpa_supplicant.conf -i wlan0 -f /var/log/wpa_supplicant.log -B
```

## Flashing https://wiki.luckfox.com/Luckfox-Pico-Plus-Mini/Flash-image

In Luckfox:
```sh
reboot loader
```
OR

The Hardware Bypass (Step-by-Step)
1. Unplug the USB cable from your Luckfox Pico Mini so it is completely powered off.
2. Locate the physical BOOT button on the board.
3. Press and hold that BOOT button down.
4. While keeping the button pressed, plug the USB cable back into your PC.
5. Give it about 2 to 3 seconds, then release the button.


In Ubuntu:
```sh
sudo ./rkflash.sh update
```


## Ubuntu

Show network interfaces:
`ip addr show`

Check all available networks:
`nmcli dev wifi list ifname wlx84fc14e66330`

Connect to Android AP:
`nmcli dev wifi connect "YourPhoneHotspot" password "YourPassword" ifname wlx84fc14e66330`

Checking signal strength:
`iw dev wlx84fc14e66330 link`




## WPA CLI
```sh
# Get signal strength
wpa_cli signal_poll

# Scan networks
wpa_cli scan

wpa_cli list_networks
```


## Capture with FFMPEG

```sh
v4l2-ctl -d /dev/video15 --all


ffmpeg -f v4l2 -use_libv4l2 1 -input_format nv12 -video_size 576x324 -framerate 30 \
  -i /dev/video15 \
  -c:v h264_v4l2m2m -b:v 2M -g 30 -bf 0 \
  -an \
  -f mpegts "udp://192.168.1.100:1234?pkt_size=1316"
```
