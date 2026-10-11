#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>

static lv_obj_t *count_label;
static lv_obj_t *touch_label;
static uint32_t clicks;

static void btn_cb(lv_event_t *e)
{
	char text[32];

	ARG_UNUSED(e);
	snprintf(text, sizeof(text), "clicked %u times", ++clicks);
	lv_label_set_text(count_label, text);
	printk("button clicked, count %u\n", clicks);
}

static void screen_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_indev_active();
	lv_point_t p;
	char text[32];

	if (indev == NULL) {
		return;
	}
	lv_indev_get_point(indev, &p);
	snprintf(text, sizeof(text), "touch %d, %d", (int)p.x, (int)p.y);
	lv_label_set_text(touch_label, text);
}

int main(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *title, *btn, *btn_label;

	if (!device_is_ready(disp)) {
		printk("display not ready\n");
		return 0;
	}

	title = lv_label_create(scr);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_label_set_text(title, "Hello YuzukiNeko");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

	btn = lv_button_create(scr);
	lv_obj_set_size(btn, 300, 100);
	lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
	btn_label = lv_label_create(btn);
	lv_label_set_text(btn_label, "Click me");
	lv_obj_center(btn_label);

	count_label = lv_label_create(scr);
	lv_label_set_text(count_label, "clicked 0 times");
	lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 90);

	touch_label = lv_label_create(scr);
	lv_label_set_text(touch_label, "touch -, -");
	lv_obj_align(touch_label, LV_ALIGN_BOTTOM_MID, 0, -40);
	lv_obj_add_event_cb(scr, screen_cb, LV_EVENT_PRESSING, NULL);

	lv_timer_handler();
	display_blanking_off(disp);
	printk("lvgl hello: %d x %d\n", (int)lv_display_get_horizontal_resolution(NULL),
	       (int)lv_display_get_vertical_resolution(NULL));

	while (1) {
		uint32_t next = lv_timer_handler();

		k_msleep(MIN(next, 1000U));
	}
	return 0;
}
