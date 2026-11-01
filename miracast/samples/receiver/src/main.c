/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <aic8800/p2p.h>
#include <miracast/player.h>

static atomic_t session_state = ATOMIC_INIT(MIRACAST_CLOSED);

static void state(void *ctx, enum miracast_state value)
{
	ARG_UNUSED(ctx);
	atomic_set(&session_state, value);
}

static void text(lv_obj_t *label, const char *message)
{
	lv_label_set_text(label, message);
	lv_obj_center(label);
	lv_timer_handler();
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	char peer[16];

	if (!device_is_ready(display) || !device_is_ready(DEVICE_DT_GET(DT_NODELABEL(ve))) ||
	    !device_is_ready(DEVICE_DT_GET(DT_NODELABEL(audio_codec))) ||
	    !device_is_ready(DEVICE_DT_GET(DT_NODELABEL(codec_analog)))) {
		printk("Miracast: display, decoder or audio device is not ready\n");
		return -ENODEV;
	}
	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);
	lv_obj_t *label = lv_label_create(lv_screen_active());

	lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), 0);
	lv_obj_set_style_bg_color(label, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(label, 24, 0);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	display_blanking_off(display);
	printk("Miracast receiver: %s\n", CONFIG_SAMPLE_WFD_NAME);

	while (true) {
		bool direct = !CONFIG_SAMPLE_WFD_PEER[0];
		int ret;

		text(label, "Starting Miracast...");
		if (direct) {
			ret = aic8800_p2p_start(CONFIG_SAMPLE_WFD_NAME, 7236);
			if (ret) {
				printk("Miracast: P2P start failed: %d\n", ret);
				text(label, "Wi-Fi Direct could not start. See serial log.");
				return ret;
			}
			printk("Miracast: discoverable; select '%s' on the phone\n", CONFIG_SAMPLE_WFD_NAME);
			text(label, CONFIG_SAMPLE_WFD_NAME "\n\nOpen Wireless display / Screen mirroring\non your Android phone and select this display.");
			int64_t last_pbc = k_uptime_get();

			while ((ret = aic8800_p2p_peer(peer, sizeof(peer))) == -EAGAIN) {
				/* The running receiver is an explicit invitation to pair. */
				if (k_uptime_get() - last_pbc >= 100000) {
					aic8800_p2p_accept();
					last_pbc = k_uptime_get();
				}
				lv_timer_handler();
				k_msleep(20);
			}
			if (ret) {
				aic8800_p2p_stop();
				printk("Miracast: peer discovery failed: %d\n", ret);
				return ret;
			}
		} else {
			if (strlen(CONFIG_SAMPLE_WFD_PEER) >= sizeof(peer)) return -EINVAL;
			strcpy(peer, CONFIG_SAMPLE_WFD_PEER);
		}
		printk("Miracast: source %s\n", peer);
		struct miracast_receiver_config config = {
			.peer = peer,
			.stream = {.rtp_port = 19000, .cea_modes = IS_ENABLED(CONFIG_SAMPLE_WFD_720P) ? 0x21 : 1},
			.ops = {.media = miracast_player_media, .state = state},
		};
		/* Some sources begin listening shortly after obtaining their address. */
		bool played = false;
		for (int attempt = 0; attempt < 10 && !played; attempt++) {
			text(label, "Connecting to the phone...");
			atomic_set(&session_state, MIRACAST_NEGOTIATING);
			ret = miracast_player_start(15);
			if (!ret) {
				ret = miracast_receiver_start(&config);
			}
			if (ret) {
				miracast_player_stop();
				printk("Miracast: receiver start: %d\n", ret);
				break;
			}
			unsigned last = 0;
			int64_t last_stats = k_uptime_get();

			while (miracast_receiver_running()) {
				if (atomic_get(&session_state) == MIRACAST_PLAYING) played = true;
				if (miracast_player_frames()) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
				if (k_uptime_get() - last_stats >= 5000) {
					unsigned n = miracast_player_frames();

					printk("Miracast: %u frames (+%u / 5 s)\n", n, n - last);
					last = n;
					last_stats = k_uptime_get();
				}
				lv_timer_handler();
				k_msleep(10);
			}
			miracast_receiver_stop();
			miracast_player_stop();
			lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
			k_msleep(500);
		}
		if (direct) aic8800_p2p_stop();
		text(label, "Disconnected. Preparing to receive again...");
		k_msleep(1000);
	}
	return 0;
}
