# Pixelflut v6 server using DPDK framework

## Pixelflut v6
Pixelflut v6 is a remake of the classical Pixelflut (v4). Pixelflut is a programming game, where multiple players can color pixels on a projector screen. The players want to draw pixels as fast as possible in their desired color to create an image on the screen. So the battle starts...

The original Pixelflut uses ASCII-commands over a a TCP-Stream (for example `PX 123 123 ffffff` to color Pixel (123,123) white). Pixelflut v6 transfers its information in the IPv6 destination address. So the client must send IPv6 Packets, the overlying protocol is irrelevant.
The Pixelflut v6 server has an /64 IPv6 subnet, so the first 64 bits of the IPv6-address are fix. The x, y coordinate and color are appended and padded with 8 bits (0s or 1s). The resulting IPv6 destination address looks as following:
`Prefix:XXXX:YYYY:RRGG:BB00`

Assuming the subnet
```
4000:1234:1234:1234:/64
```
for the server, if you do
```
ping 4000:1234:1234:1234:007B:007B:ffff:ff00
```
you send an ICMP packet to the server, coloring the pixel (0x7b,0x7b) white. (0x7b = 123).

## Preparing & Building the server
The server uses the DPDK (https://www.dpdk.org/) framework, so your NIC must be supported by DPDK. Check this with http://core.dpdk.org/supported/.
DPDK unbinds the NIC from the kernel, so it cannot be used for anything else than pixelflut v6. So if you want to watch the game via VNC or keep your ssh session to the server, your server must have mutliple NICs oder ports.

The pixelflut v6 server is heavily copied from https://github.com/TobleMiner/shoreline. I recommend starting a vnc frontend, so that the server acts as a vnc-server and multiple clients can watch the game with a VNC client. It should be mentioned, that a client can use up to 1.5 Gbps, so a 10G NIC is recommended for the people watching having a smoth experience.

### Download and build DPDK
Download and build DPDk from sources at https://core.dpdk.org/download/. This server was written for DPDK version 19.05. Herefore see https://doc.dpdk.org/guides/linux_gsg/sys_reqs.html and https://doc.dpdk.org/guides/linux_gsg/build_dpdk.html.
For example for an ConnectX-3 VPI dual port from Mellanox on an debian buster following steps are needed.
For details see https://doc.dpdk.org/guides/nics/mlx4.html.
```
# Build dpdk
sudo apt install libibverbs-dev librdmacm-dev libnuma-dev linux-headers-$(uname -r) libmnl-dev
wget fast.dpdk.org/rel/dpdk-19.05.tar.xz
tar xf dpdk-19.05.tar.xz
cd dpdk-19.05
sed -i -r 's/CONFIG_RTE_LIBRTE_MLX4_PMD=n/CONFIG_RTE_LIBRTE_MLX4_PMD=y/' config/common_base
# MLX5: sed -i -r 's/CONFIG_RTE_LIBRTE_MLX5_PMD=n/CONFIG_RTE_LIBRTE_MLX5_PMD=y/' config/common_base
# echo "CONFIG_RTE_LIBRTE_MLX4_PMD=y" >> config/common_linux
make config T=x86_64-native-linux-gcc
make -j 40
sudo make install

# Set ports to ethernet
echo eth > /sys/bus/pci/devices/0000\:42\:00.0/mlx4_port1
echo eth > /sys/bus/pci/devices/0000\:42\:00.0/mlx4_port2

# Load kernel modules
modprobe -a ib_uverbs mlx4_en mlx4_core mlx4_ib
# MLX5: modprobe -a ib_uverbs mlx5_core mlx5_ib

# This will find the cards and try to start the server, but will cause the following error message:
# PMD: net_mlx4: 0x55e60ddae900: cannot attach flow rules (code 93, "Protocol not supported"), flow error type 2, cause 0x17e55d640, message: flow rule rejected by device
# To fix this:
echo "options mlx4_core log_num_mgm_entry_size=-7" >> /etc/modprobe.d/mlx4_core.conf
update-initramfs -u

# If you get: net_mlx5: probe of PCI device aborted after encountering an error: Operation not supported
# update to a newer version of libibverbs with
echo "deb http://deb.debian.org/debian buster-backports main" >> /etc/apt/sources.list
apt-get -t buster-backports install libibverbs-dev
```

### Allocate hugepages
You can either allocate hugepages at runtime or via kernel boot-option. I suggest the second method, so can allocate 1GB hugepages instead of 2MB, but the first method should work also.

Method 1, at runtime:
```
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```

Method 2, boot options:
```
default_hugepagesz=1G hugepagesz=1G hugepages=4
```
To do so for example change the line `GRUB_CMDLINE_LINUX_DEFAULT="pci=realloc=off default_hugepagesz=1G hugepagesz=1G hugepages=8"` in /etc/default/grub. After this run `update-grub` to apply the changes.

### Unbind your NIC-port from kernel
See https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html.
Basically do
```
export RTE_SDK=/my/path/to/dpdk/folder
$RTE_SDK/usertools/dpdk-devbind.py --status
modprobe uio_pci_generic
# optional, if NIC is active take it first down: ip link set dev eno1 down
$RTE_SDK/usertools/dpdk-devbind.py --bind=uio_pci_generic 0000:00:19.0 # Change to your pci-adress
```

### Install needed libraries for Pixelflut v6 server
```
apt install libnuma-dev linux-source linux-headers-$(uname -r) libsdl2-dev git build-essential libsdl2-dev libpthread-stubs0-dev libvncserver-dev libnuma-dev
```

### Build Pixelflut v6 server
Simple build it with
```
cd server
export RTE_SDK=/my/path/to/dpdk/folder
make
```

## Running the Pixelflut v6 server
```
pixelflut_v6 [EAL options for DPDK] -- -p PORTMASK
  -p PORTMASK: hexadecimal bitmask of ports to configure (for example 0x3 for the first 2 ports)
  -c NUMBER: number of cores per port (default is 1)
  -q NUMBER: number of queus per core (default is 1)
  -T PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 1 default, 86400 maximum)
```

## Performance
A Mellanox ConnectX-3 VPI (MCX354A-FCBT) card for 50€ was used for performance testing.
Is a dual port NIC, one port was used as client, one as server.
The server was able of handling 12 Mpps and arround 6 Gbps. I think the bottleneck was the controller on the "old" ConnectX-3 card and not the software running.
The client was able to produce 30 Mpps.
It is possible to add a payload to the IPv6 Packets (yes, pretty useless) to saturate the 40G link.

It would be very interesting to see the performance on a ConnectX-4 or higher card.
