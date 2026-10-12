/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_GAMES_GAME_H_
#define NEKOBOX_FEATURES_GAMES_GAME_H_

/** Starts a game (.nes .gb .gbc .gba); the cross of the game screen returns to @p from */
void ui_game_open(const char *path, const char *from);

#endif /* NEKOBOX_FEATURES_GAMES_GAME_H_ */
