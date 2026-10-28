#ifndef CR_HOTKEY_HOOK_H
#define CR_HOTKEY_HOOK_H

extern volatile int g_hotkey_hook_running;
void *hotkey_hook_thread(void *arg);

#endif /* CR_HOTKEY_HOOK_H */
