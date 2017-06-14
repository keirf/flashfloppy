/*
 * speaker.h
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#ifdef BUILD_TOUCH

void speaker_init(void);
void speaker_pulse(uint8_t volume);

#else

static inline void speaker_init(void) {}
static inline void speaker_pulse(uint8_t volume) {}

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
