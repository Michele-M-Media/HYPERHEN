#ifndef CR_BROWSER_H
#define CR_BROWSER_H

#ifndef CHEATRUNNER_HAVE_BROWSER_OPEN
#define CHEATRUNNER_HAVE_BROWSER_OPEN 0
#endif

/* Opens the system browser at url via sceSystemServiceLaunchWebBrowser; overlay behavior is unconfirmed, test on console. */
int cr_browser_open_url(const char *url);

#endif /* CR_BROWSER_H */
