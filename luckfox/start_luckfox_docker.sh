#!/bin/bash

sudo docker container rm luckfox
sudo docker run -it --ipc=host --privileged --name luckfox \
	-v /home/pswidersk/REPOS/luckfox-pico:/home/ \
	-v /home/pswidersk/REPOS/cyber-eye/luckfox/overlay:/home/project/cfg/BoardConfig_IPC/overlay/custom-overlay \
	luckfoxtech/luckfox_pico:1.0 /bin/bash
