/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootstrap_hyperdeck.h"
#include "hyperdeck_backend.hpp"
#include <onion/log.h>
#include <dlfcn.h>

extern "C" int sceUserServiceInitialize(void *);

extern "C" int bootstrap_hyperdeck_install_media(void) {
  // The parent performs the same platform preparation as the reference's
  // Media installer. No new startup-time module dependency is added to core.
  void *network = dlopen("/system/common/lib/libSceNetCtl.sprx", RTLD_LAZY);
  auto net_init = network ? reinterpret_cast<int (*)(void)>(
      dlsym(network,"sceNetCtlInit")) : nullptr;
  const int net_result = net_init ? net_init() : -1;
  int priority = 256;
  const int user_result = sceUserServiceInitialize(&priority);
  LOG_INFO("HyperDeck parent Media services net=0x%08x user=0x%08x",
           static_cast<unsigned>(net_result), static_cast<unsigned>(user_result));
  const int result = hyperdeck::install_media_launcher() ? 0 : -1;
  if (network) dlclose(network);
  return result;
}
