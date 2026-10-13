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

## Start and stop

`aic8800_start()` / `aic8800_stop()` (`include/aic8800/aic8800.h`). The driver starts on first use
(scan, join, the Bluetooth transport) and stays until it is stopped. A stop leaves nothing behind:

1. the network is left and the card interrupt is switched off;
2. every task and timer the driver made is stopped (`rtos_al_shutdown()`, they are listed in the
   RTOS layer);
3. the chip is powered off (reg_on low, the Bluetooth reset held);
4. the static data of the driver (`aic_state.ld`: its `.data` and `.bss`, not the glue to the
   network interface, the pool and the task list) is put back to the values it had at the boot;
5. the memory pool is given back to the malloc arena.

The next start downloads the firmware again (about 2.5 s, a stop takes 1.3 s). With the driver
stopped the arena is one block again. Bluetooth must be disabled before the stop. Checked on the
board: five rounds of start, scan (18-25 networks each) and stop, with the arena back to a single
block of the same size after each stop (`NEKOBOX_RADIO_TEST` of the application).

## Memory

The driver, the supplicant and the packet buffers take their memory from a pool of their own
(`CONFIG_AIC8800_MEM_POOL_KB`, 640 KiB, taken from the malloc arena at the start and given back at the stop, `src/aic_mem.c`): `malloc`,
`calloc`, `realloc` and `aligned_alloc` of the sources of the module are redirected to it by
`include/aic_mem_redirect.h`, and `free()` of the whole image is wrapped (`--wrap=free`) so that a
pointer of the pool goes back to the pool. A request the pool cannot serve fails; it does not take
the memory of the malloc arena. Before, the small objects of the driver split the arena and the
largest free block was 6 MB of 11; now it stays whole (9.8 MB with the pool and Wi-Fi up). The
most the driver has used is printed when the link comes up (about 310 KiB after connecting).
