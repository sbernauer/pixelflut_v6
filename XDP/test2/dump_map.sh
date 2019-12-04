#!/bin/bash

bpfToolPath="/home/sbernauer/Desktop/Pixelflut/linux/tools/bpf/bpftool/bpftool"

MAP_ID=$(sudo $bpfToolPath map show | tail -n 2 | grep -oP '[0-9]+(?=: )')
sudo $bpfToolPath map dump id $MAP_ID
