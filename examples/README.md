# examples

Small applications for yuzukineko, one per driver or library interface: every function gets a complete
example and the board output is in the documentation (`dev/zephyr/` in YuzukiNeko).

| Directory | What |
|---|---|
| `gpio_api`, `i2c_api`, `spi_api`, `pwm_api`, `pwm_bl_api`, `uart_api` | the GPIO, I2C, SPI, PWM, analog backlight PWM and UART drivers |
| `display_api` | the display pipeline; `tools/init2dts.py` converts a panel vendor's `INIT.txt` to `init-sequence` |
| `sdhc_api`, `sd_api` | the SD host controller by hand; the SD subsystem, disk access and FAT file system |
| `audio_api`, `aac_api` | codec, I2S0 and S/PDIF; the AAC decoder (with test clips and a host build for comparison) |
| `usb_api`, `cherryusb_api`, `cherryusb_host_api` | the USB device port; the CherryUSB device stack (CDC ACM, MSC) and host stack |
| `g2d_api`, `vdec_api` | the 2D accelerator and the video decoder |
| `mp4_api` | the MP4 demuxer (with a short clip) |
| `aic8800_wifi_api`, `aic8800_bt_api` | WiFi and Bluetooth of the AIC8800 |
| `lvgl_hello` | a minimal LVGL user interface with a button |

Build from the west workspace, with the board name `yuzukineko`:

```
. ./env.sh
west build -b yuzukineko -d build/gpio zephyr-components/examples/gpio_api
```

Download `build/gpio/zephyr/zephyr.bin` as described in the documentation (`xfel ddr f101-s3`,
`xfel write 0x40010000 zephyr.bin`, `xfel exec 0x40010000`). The console is UART1 (PB0/PB1), 115200 8N1.
