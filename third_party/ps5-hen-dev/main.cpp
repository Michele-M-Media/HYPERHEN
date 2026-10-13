#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <print>

extern "C" {
#include <ps5/kernel.h>
}

#include "hv_defeat.h"
#include "util.h"

namespace {
constexpr const char *kHyperHENRoot = "/system_tmp/hyperhen";
constexpr const char *kHyperHENReady = "/system_tmp/hyperhen/ready";
constexpr const char *kHyperHENLegacyReady = "/system_tmp/hyperhen/ready/legacy_hen";
constexpr const char *kHyperHENLegacyFailed = "/system_tmp/hyperhen/ready/legacy_hen_failed";

void hyperhen_legacy_mark(const char *path, const char *state)
{
    (void)mkdir(kHyperHENRoot, 0777);
    (void)mkdir(kHyperHENReady, 0777);

    FILE *fp = fopen(path, "w");
    if (fp != nullptr) {
        (void)fputs(state, fp);
        (void)fputc('\n', fp);
        (void)fclose(fp);
    }
}

void hyperhen_legacy_begin()
{
    (void)mkdir(kHyperHENRoot, 0777);
    (void)mkdir(kHyperHENReady, 0777);
    (void)unlink(kHyperHENLegacyReady);
    (void)unlink(kHyperHENLegacyFailed);
}
}

int main()
{
    hyperhen_legacy_begin();
    const int result = run_hv_defeat();
    hyperhen_legacy_mark(result == 0 ? kHyperHENLegacyReady : kHyperHENLegacyFailed,
                         result == 0 ? "ready" : "failed");
    return result;
}
