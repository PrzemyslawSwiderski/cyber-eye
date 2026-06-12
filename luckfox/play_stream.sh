#!/bin/bash

ffplay -protocol_whitelist file,udp,rtp -fflags nobuffer -flags low_delay -i stream.sdp
