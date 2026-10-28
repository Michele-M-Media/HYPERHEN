#ifndef CR_HOTKEY_BUTTONS_H
#define CR_HOTKEY_BUTTONS_H

#include <stdint.h>
#include <string.h>

/* Values captured from buttons_down on a console; Mono AOT strips the enum metadata, so there is no other source. */
typedef struct {
  const char *name;
  uint32_t    value;
} cr_hotkey_button_t;

static const cr_hotkey_button_t CR_HOTKEY_BUTTONS[] = {
  { "Left",     0x1u },
  { "Up",       0x2u },
  { "Right",    0x4u },
  { "Down",     0x8u },
  { "Square",   0x10u },
  { "Triangle", 0x20u },
  { "Circle",   0x40u },
  { "Cross",    0x80u },
  { "Options",  0x100u },
  { "L1",       0x400u },
  { "R1",       0x800u },
  { "L2",       0x1000u },
  { "R2",       0x2000u },
  { "L3",       0x4000u },
  { "R3",       0x8000u },
  { "TouchPad", 0x40000u },
};
#define CR_HOTKEY_BUTTON_COUNT (sizeof(CR_HOTKEY_BUTTONS) / sizeof(CR_HOTKEY_BUTTONS[0]))

/* Returns 0 (no bits) for an unrecognized name - callers treat that as invalid. */
static inline uint32_t
cr_hotkey_button_value(const char *name) {
  for (size_t i = 0; i < CR_HOTKEY_BUTTON_COUNT; i++) {
    if (!strcmp(CR_HOTKEY_BUTTONS[i].name, name)) {
      return CR_HOTKEY_BUTTONS[i].value;
    }
  }
  return 0;
}

#endif /* CR_HOTKEY_BUTTONS_H */
