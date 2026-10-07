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
| `compat/` | small headers for what the minimal C library lacks |

Status: **builds, not run on hardware** (no module was available). Check the first bring-up with the
sample, watching for the firmware download and the chip ID in the log.

```
west build -b f101_evb -d build/wifi zephyr-components/aic8800/samples/wifi_scan \
    -- -DCONFIG_SAMPLE_WIFI_SSID=\"name\" -DCONFIG_SAMPLE_WIFI_PSK=\"secret\"
```

The F101 EVB wires the module to SMHC2 on PE0..PE5 (mux 9), the power pin is PA00 and the wake
pin PF06. PE02 is also the USB host VBUS switch and PE0..PE4 are I2S0, so the overlay of the
sample turns the USB host off.

The firmware images are the large headers in `host/drv/fw` and are linked into the image.
The licence of the driver files is the one in their headers; the glue is Apache-2.0.
