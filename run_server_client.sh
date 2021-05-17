#!/bin/bash

device/device.out
TrafficGenerator/bin/server -p 5001 -d
TrafficGenerator/bin/client -b 900 -c conf/client_config.txt -n 10000 -l flows.txt -s 323 -r bin/result.py
