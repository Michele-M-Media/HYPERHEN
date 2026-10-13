/* Host tests for libonion_platform onion_net_get_ip_address. */
#include "test_harness.h"

#include <onion/net.h>
#include <ps5/net_ctl.h>

#include <string.h>

/* Driven by the sceNetCtlGetInfo stub in stubs/host_stubs.c. */
void onion_test_netctl_set_result(int32_t ret, const char *ip_address);

static int test_ip_ok(void) {
  char buf[ONION_NET_IP_ADDRESS_SIZE];

  onion_test_netctl_set_result(0, "192.168.1.50");
  TEST_ASSERT_EQ_INT(ONION_NET_IP_OK,
                     onion_net_get_ip_address(buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strcmp(buf, "192.168.1.50") == 0);
  return 0;
}

/*
 * The error codes arrive as a positive 0x8041xxxx bit pattern. The old local
 * implementations stored the result in `unsigned int` and tested `ret < 0`,
 * so this branch was unreachable and failures were reported as success.
 */
static int test_disconnected_maps_and_writes_placeholder(void) {
  char buf[ONION_NET_IP_ADDRESS_SIZE];

  memset(buf, 'A', sizeof(buf));
  onion_test_netctl_set_result((int32_t)SCE_NET_CTL_ERROR_NOT_CONNECTED, NULL);
  TEST_ASSERT_EQ_INT(ONION_NET_IP_DISCONNECTED,
                     onion_net_get_ip_address(buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strcmp(buf, ONION_NET_IP_UNKNOWN) == 0);
  return 0;
}

static int test_unavailable_maps(void) {
  char buf[ONION_NET_IP_ADDRESS_SIZE];

  onion_test_netctl_set_result((int32_t)SCE_NET_CTL_ERROR_NOT_AVAIL, NULL);
  TEST_ASSERT_EQ_INT(ONION_NET_IP_UNAVAILABLE,
                     onion_net_get_ip_address(buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strcmp(buf, ONION_NET_IP_UNKNOWN) == 0);
  return 0;
}

static int test_other_error_maps(void) {
  char buf[ONION_NET_IP_ADDRESS_SIZE];

  onion_test_netctl_set_result(-1, NULL);
  TEST_ASSERT_EQ_INT(ONION_NET_IP_ERROR,
                     onion_net_get_ip_address(buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strcmp(buf, ONION_NET_IP_UNKNOWN) == 0);
  return 0;
}

static int test_rejects_bad_buffer(void) {
  char small[ONION_NET_IP_ADDRESS_SIZE - 1];

  onion_test_netctl_set_result(0, "10.0.0.1");
  TEST_ASSERT_EQ_INT(ONION_NET_IP_BAD_BUFFER,
                     onion_net_get_ip_address(NULL, sizeof(small)));
  TEST_ASSERT_EQ_INT(ONION_NET_IP_BAD_BUFFER,
                     onion_net_get_ip_address(small, sizeof(small)));
  return 0;
}

/*
 * A field that fills all 16 bytes leaves no room for a NUL (the stub copies
 * with strncpy semantics, like the kernel does). The helper must terminate it
 * rather than hand the caller an unterminated string.
 */
static int test_terminates_unterminated_field(void) {
  char buf[ONION_NET_IP_ADDRESS_SIZE];

  onion_test_netctl_set_result(0, "1234567890123456");
  TEST_ASSERT_EQ_INT(ONION_NET_IP_OK,
                     onion_net_get_ip_address(buf, sizeof(buf)));
  TEST_ASSERT_EQ_INT('\0', buf[ONION_NET_IP_ADDRESS_SIZE - 1]);
  TEST_ASSERT_EQ_INT(15, (int)strlen(buf));
  return 0;
}

static int test_has_ipv4_follows_get_info(void) {
  onion_test_netctl_set_result(0, "10.0.0.2");
  TEST_ASSERT_TRUE(onion_net_has_ipv4());
  onion_test_netctl_set_result((int32_t)SCE_NET_CTL_ERROR_NOT_CONNECTED, NULL);
  TEST_ASSERT_TRUE(!onion_net_has_ipv4());
  return 0;
}

int test_platform_net_suite(void) {
  int failures = 0;
  failures += onion_test_run("net.ip_ok", test_ip_ok);
  failures += onion_test_run("net.disconnected",
                             test_disconnected_maps_and_writes_placeholder);
  failures += onion_test_run("net.unavailable", test_unavailable_maps);
  failures += onion_test_run("net.other_error", test_other_error_maps);
  failures += onion_test_run("net.bad_buffer", test_rejects_bad_buffer);
  failures +=
      onion_test_run("net.terminates_field", test_terminates_unterminated_field);
  failures += onion_test_run("net.has_ipv4", test_has_ipv4_follows_get_info);
  return failures;
}
