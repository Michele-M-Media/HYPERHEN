#ifndef CR_TILE_PKG_H
#define CR_TILE_PKG_H

/* Spawns the home-screen tile auto-install thread; safe to call unconditionally (no-op if not embedded or already installed). */
void cr_tile_autoinstall_init(void);

#endif /* CR_TILE_PKG_H */
