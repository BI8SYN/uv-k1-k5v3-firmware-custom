/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>


void FONT_DrawBigGlyph(uint8_t glyph, uint8_t *top, uint8_t *bottom);
extern const uint8_t gFontBigDigits[11][26 - 6];
extern const uint8_t gFont3x5[96][3];

#define FONT_SMALL_WIDTH       6u
#define FONT_SMALL_GLYPH_COUNT (95u - 1u)

// The small fonts handed to UI_PrintStringBuffer(): uncompressed asset tables
// (FONT_SMALL_WIDTH bytes per glyph), or 7-bit packed tables in internal flash.
#ifdef ENABLE_FEAT_F4HWN_FW_ASSETS
    extern const uint8_t gFontSmall[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
    #define FONT_SMALL_NORMAL ((const uint8_t *)gFontSmall)
    #ifdef ENABLE_SMALL_BOLD
        extern const uint8_t gFontSmallBold[FONT_SMALL_GLYPH_COUNT][FONT_SMALL_WIDTH];
        #define FONT_SMALL_BOLD ((const uint8_t *)gFontSmallBold)
    #endif
#else
    #define FONT_SMALL_PACKED_SIZE ((FONT_SMALL_GLYPH_COUNT * FONT_SMALL_WIDTH * 7u + 7u) / 8u)

    extern const uint8_t gFontSmallPacked[FONT_SMALL_PACKED_SIZE];
    #define FONT_SMALL_NORMAL gFontSmallPacked
    #ifdef ENABLE_SMALL_BOLD
        extern const uint8_t gFontSmallBoldPacked[FONT_SMALL_PACKED_SIZE];
        #define FONT_SMALL_BOLD gFontSmallBoldPacked
    #endif
#endif
#ifndef FONT_SMALL_BOLD
    #define FONT_SMALL_BOLD FONT_SMALL_NORMAL
#endif

#endif
