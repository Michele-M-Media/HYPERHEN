#include <stddef.h>

#include "cr_log.h"
#include "cr_browser.h"

/* Declared in ps5sdk_compat.h; forward-declared here to keep this module light. */
int sceSystemServiceLaunchWebBrowser(const char *uri, void *reserved);

int
cr_browser_open_url(const char *url) {
  if (!url || !url[0]) return -1;
  cr_log("info", "browser", "opening url=%s", url);
  int rc = sceSystemServiceLaunchWebBrowser(url, NULL);
  cr_log("info", "browser", "sceSystemServiceLaunchWebBrowser rc=0x%08X", (unsigned)rc);
  return rc;
}
