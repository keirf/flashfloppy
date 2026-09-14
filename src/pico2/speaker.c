/*
 * pico2/speaker.c
 *
 * Piezo speaker on GPIO 15. Reuses the generic Gotek speaker driver,
 * which is hardware-independent apart from its pin assignment.
 *
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define PICO2_SPEAKER_PIN 15

#include "../gotek/speaker.c"

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
