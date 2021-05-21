#!/bin/bash
device/device.out &
TrafficGenerator/bin/client -b 800 -c TrafficGenerator/conf/client_config.txt -n 10000 -l flows.txt -s 323 -r TrafficGenerator/bin/result.py > log1.txt &
TrafficGenerator/bin/client -b 800 -c TrafficGenerator/conf/client_config.txt -n 10000 -l flows.txt -s 323 -r TrafficGenerator/bin/result.py > log1.txt &
TrafficGenerator/bin/client -b 800 -c TrafficGenerator/conf/client_config.txt -n 10000 -l flows.txt -s 323 -r TrafficGenerator/bin/result.py