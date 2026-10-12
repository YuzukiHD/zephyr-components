# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_USB_DISPLAY app PRIVATE usb_display.c usb_touch.c)
