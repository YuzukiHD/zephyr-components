/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The driver prints in pieces ("### dst" and later "=0x..., size=..\n"), so the text is collected
 * until a newline and then handed to the logging system as one message per line.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(aic8800, CONFIG_AIC8800_LOG_LEVEL);

#define LINE_MAX_LEN	255

static char pending[LINE_MAX_LEN + 1];
static size_t pending_len;
static struct k_spinlock pending_lock;

static bool has_word(const char *line, const char *word)
{
	size_t n = strlen(word);

	for (; *line != '\0'; line++) {
		if (strncasecmp(line, word, n) == 0) {
			return true;
		}
	}
	return false;
}

static void emit(unsigned int level, const char *line)
{
	/* most failures are reported at the level of the call that prints them, with a word */
	if (level > AIC_LVL_ERR && (has_word(line, "fail") || has_word(line, "error"))) {
		level = AIC_LVL_ERR;
	}

	switch (level) {
	case AIC_LVL_ERR:
		LOG_ERR("%s", line);
		break;
	case AIC_LVL_WRN:
		LOG_WRN("%s", line);
		break;
	case AIC_LVL_DBG:
		LOG_DBG("%s", line);
		break;
	default:
		LOG_INF("%s", line);
		break;
	}
}

int aic_log(unsigned int level, const char *fmt, ...)
{
	char text[LINE_MAX_LEN + 1];
	char line[LINE_MAX_LEN + 1];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintk(text, sizeof(text), fmt, ap);
	va_end(ap);
	if (n < 0) {
		return n;
	}

	for (const char *p = text; *p != '\0'; p++) {
		bool done = false;

		K_SPINLOCK(&pending_lock) {
			if (*p == '\n') {
				memcpy(line, pending, pending_len);
				line[pending_len] = '\0';
				pending_len = 0;
				done = true;
			} else if (*p != '\r' && pending_len < LINE_MAX_LEN) {
				pending[pending_len++] = *p;
			}
		}
		if (done && line[0] != '\0') {
			emit(level, line);
		}
	}

	return n;
}
