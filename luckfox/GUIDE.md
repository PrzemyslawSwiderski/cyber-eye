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

## Video view

Stream from Luckfox:
```sh
/oem/usr/bin/RkLunch.sh

ffmpeg -f v4l2 -framerate 30 -video_size 1280x720 -i /dev/video0 -c:v h264 -f rtp "rtp://192.168.1.12:5600"
```


USB network:
`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport tcp rtsp://172.32.0.93:554/live/0`

Nju network:
`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport tcp rtsp://192.168.1.15:554/live/0`

## Build image


Build with docker:
`sudo docker pull luckfoxtech/luckfox_pico:1.0`

Start an instance with pseudo-TTY:
```sh
sudo docker run -it --privileged --name luckfox \
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
```
insmod cfg80211.ko
insmod 8812eu.ko
```

Save new network config:
```
network={
     ssid="myssid"
     psk="12345678"
}
```
in `/etc/wpa_supplicant.conf` file.

connect to network:
```
wpa_supplicant -D nl80211 -c /etc/wpa_supplicant.conf -i wlan0
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
