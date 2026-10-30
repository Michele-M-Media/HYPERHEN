#pragma once

#include "hyper_dialogue.hpp"

// The readiness side effects are unreachable until the real model preparation
// succeeds. The caller owns the bridge/marker lifetime on every exit path.
template <typename Prepare, typename Stopping, typename Activate>
HyperDialogueStatus hyper_initialize_before_ready(Prepare prepare, Stopping stopping, Activate activate) {
    const auto status = prepare();
    if (status != HyperDialogueStatus::ok) return status;
    if (stopping()) return HyperDialogueStatus::cancelled;
    return activate() ? HyperDialogueStatus::ok : HyperDialogueStatus::internal_error;
}
