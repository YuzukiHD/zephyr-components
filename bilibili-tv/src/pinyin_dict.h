/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_PINYIN_DICT_H_
#define BILI_PINYIN_DICT_H_

#include <lvgl.h>

/** Syllable -> characters, sorted, ends with {NULL, NULL}; for lv_ime_pinyin_set_dict() */
extern const lv_pinyin_dict_t bili_pinyin_dict[];

#endif /* BILI_PINYIN_DICT_H_ */
