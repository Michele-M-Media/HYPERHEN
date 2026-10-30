#ifndef HYPER_P5_NATIVE_INPUT_H
#define HYPER_P5_NATIVE_INPUT_H

#include "activation_gate.h"

/* A separately reviewed, statically linked native input provider may replace
 * this weak symbol. It must return one fresh sample of the currently paired
 * user's controller, or a negative error. The default fails closed. No IPC
 * or untrusted remote input can request microphone activation. */
int hyper_p5_native_read_input(struct hyper_p5_sample *sample);

#endif
