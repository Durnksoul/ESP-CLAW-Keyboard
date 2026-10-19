/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* ESP32-S3-COREBOARD V1.4 schematic: GPIO48 -> RGB (0 ohm) -> U1 DI.
 * U1 is XL-5050RGBC-WS2812B. GPIO7 is the separate keyboard PCB RGB chain.
 */
#define KEYBOARD_BOARD_RGB_GPIO 48

/* Keyboard base schematic: RGB via R7, encoder A/B, switch ROW1/COL4. */
#define KEYBOARD_BOARD_KEY_RGB_GPIO 7
/* 6028/3528 WS2812B: 24-bit GRB, 800 kbit/s.
 * XL-3528RGBW-WS2812B datasheet p8 (part name RGBW does not mean 32-bit):
 * https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8903/XL-3528RGBW-WS2812B.pdf
 * Use 50 ns ticks to represent its typical timings exactly.
 */
#define KEYBOARD_BOARD_KEY_RGB_RESOLUTION_HZ 20000000
#define KEYBOARD_BOARD_KEY_RGB_T0H_NS 250
#define KEYBOARD_BOARD_KEY_RGB_T0L_NS 1000
#define KEYBOARD_BOARD_KEY_RGB_T1H_NS 850
#define KEYBOARD_BOARD_KEY_RGB_T1L_NS 400
#define KEYBOARD_BOARD_KEY_RGB_RESET_US 400
#define KEYBOARD_BOARD_LED_TO_KEY {0, 1, 2, 3, 4, 5, 6, 7, 8}
#define KEYBOARD_BOARD_ENCODER_A_GPIO 12
#define KEYBOARD_BOARD_ENCODER_B_GPIO 13
#define KEYBOARD_BOARD_ENCODER_BUTTON_COL_GPIO 8
#define KEYBOARD_BOARD_ENCODER_DIRECTION 1
