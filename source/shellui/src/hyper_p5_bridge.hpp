#pragma once
#include <cstdint>

/* Passive observer of the existing ShellUI GamePad.GetData result. */
void hyper_p5_bridge_observe(int device_index, bool skipped, uint32_t buttons);

/* Call from the initialized payload thread, never from GetData/OnRender. */
void hyper_p5_bridge_start();

/* Settings/UI threads publish the master switch; audio threads read atomically. */
void hyper_p5_bridge_set_enabled(bool enabled);
