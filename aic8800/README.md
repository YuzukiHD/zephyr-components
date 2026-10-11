# aic8800

Zephyr module for the AIC8800 WiFi chips (D, DW, DC = D40, D80) on an SDIO bus: the FullMAC
host driver (command queue, TX/RX descriptors, firmware download for every chip variant), the
supplicant (station, access point, P2P, WPS, Hotspot 2.0, SAE), and the glue to the Zephyr
kernel, SDIO stack and network stack (`net_if` with `wifi_mgmt`: scan, connect, disconnect, DHCPv4).

The sources of the driver and of the supplicant are kept as they are; the layers that depend on the
operating system are separate directories named `zephyr` next to the original ones:

| Directory | |
|---|---|
| `host/rtos_al/zephyr` | tasks, queues, semaphores, mutexes, timers on Zephyr kernel objects |
| `host/platform/zephyr` | SDIO transport on the Zephyr `sd` subsystem, power pin, interrupt pin |
| `host/net_al/zephyr` | network interface, replacing the TCP/IP stack the driver was written against |
| `compat/` | small headers for what the C library lacks |

Status: builds and runs on the `yuzukineko` board. Chip identification, firmware download and
scanning are checked on the hardware; joining a network needs an access point and its credentials.

The board file of `yuzukineko` already has the `aic8800` node under `smhc2` (SDIO on PE0..PE5, mux 9, 4 bit),
REG_ON on PE10 and the Bluetooth reset on PD8, so the samples need no overlay. PE2 is the SDIO clock, so
the USB host (VBUS switch) is not available on this board and the I2S0 pins are taken.

```
west build -b yuzukineko -d build/wifi zephyr-components/aic8800/samples/wifi_scan
west build -b yuzukineko -d build/wifi-iperf zephyr-components/aic8800/samples/wifi_iperf3
west build -b yuzukineko -d build/bt-scan zephyr-components/aic8800/samples/bt_scan
```

`wifi_scan` joins a network after the scan when `CONFIG_SAMPLE_WIFI_SSID` and `CONFIG_SAMPLE_WIFI_PSK` are set
(`-- -DCONFIG_SAMPLE_WIFI_SSID=\"name\" -DCONFIG_SAMPLE_WIFI_PSK=\"secret\"`); without them it only scans.
The driver does not print the passphrase in the log.

The firmware images are the large headers in `host/drv/fw` and are linked into the image.
The licence of the driver files is the one in their headers; the glue is Apache-2.0.
