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


## Connect by telnet

`telnet 172.32.0.93`

Initial Login: root
Password: luckfox

## Video view

`ffplay -fflags nobuffer -flags low_delay -framedrop -strict experimental -rtsp_transport tcp rtsp://172.32.0.93:554/live/0`

## Build image


Build with docker:
`sudo docker pull luckfoxtech/luckfox_pico:1.0`

Start an instance with pseudo-TTY:
```sh
sudo docker run -it --name luckfox \
      -v /home/pswidersk/REPOS/cyber-eye/luckfox/overlay:/opt/overlay \
      -v /home/pswidersk/REPOS/luckfox-pico:/home \
      -v /home/pswidersk/REPOS/cyber-eye/luckfox/external:/home/external \
      luckfoxtech/luckfox_pico:1.0 /bin/bash
```

If the container already exists, you can restart it using the following command:
`sudo docker start -ai luckfox`


Build SDK:
```sh
cd /home
./build.sh lunch
./build.sh 
```

Flashing:

https://wiki.luckfox.com/Luckfox-Pico-Plus-Mini/Flash-image#51-flashing-image-to-spi-nand-flash



## Ubuntu

Show network interfaces:
`ip addr show`

Check all available networks:
`nmcli dev wifi list ifname wlx84fc14e66330`

Connect to Android AP:
`nmcli dev wifi connect "YourPhoneHotspot" password "YourPassword" ifname wlx84fc14e66330`

Checking signal strength:
`iw dev wlx84fc14e66330 link`
