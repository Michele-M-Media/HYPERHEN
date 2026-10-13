#include "test_harness.h"

#include <onion/app_jailbreak_policy.hpp>

static int test_itemzflow_is_whitelisted(void) {
  const onion::AppJailbreakAllowlist allowlist{};
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("ITEM00001", allowlist));
  TEST_ASSERT_STREQ("exact",
                    onion::app_jailbreak::whitelist_reason("ITEM00001",
                                                           allowlist));
  return 0;
}

static int test_existing_policy_is_preserved(void) {
  const onion::AppJailbreakAllowlist allowlist{};
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("NPXS39041", allowlist));
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("PKGI13337", allowlist));
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("PKGI12345", allowlist));
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("TOOL00001", allowlist));
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("LAPY12345", allowlist));
  TEST_ASSERT_TRUE(
      !onion::app_jailbreak::is_whitelisted("CUSA12345", allowlist));
  TEST_ASSERT_STREQ("prefix", onion::app_jailbreak::whitelist_reason(
                                    "LAPY12345", allowlist));
  TEST_ASSERT_STREQ("none",
                    onion::app_jailbreak::whitelist_reason("CUSA12345",
                                                           allowlist));
  /* Prefix matching must not turn into an unanchored substring match. */
  TEST_ASSERT_TRUE(
      !onion::app_jailbreak::is_whitelisted("XLAPY1234", allowlist));
  return 0;
}

static int test_configured_policy_replaces_defaults(void) {
  onion::AppJailbreakAllowlist allowlist{};
  allowlist.exact_title_ids = {};
  allowlist.exact_title_ids[0] = "CUSA12345";
  allowlist.exact_title_id_count = 1;
  allowlist.title_id_prefixes = {};
  allowlist.title_id_prefixes[0] = "ABCD";
  allowlist.title_id_prefix_count = 1;

  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("CUSA12345", allowlist));
  TEST_ASSERT_TRUE(
      onion::app_jailbreak::is_whitelisted("ABCD00001", allowlist));
  TEST_ASSERT_TRUE(
      !onion::app_jailbreak::is_whitelisted("ITEM00001", allowlist));
  TEST_ASSERT_TRUE(
      !onion::app_jailbreak::is_whitelisted("LAPY12345", allowlist));
  return 0;
}

extern "C" int test_app_jailbreak_policy_suite(void) {
  int failures = 0;
  failures += onion_test_run("app_jailbreak.itemzflow_whitelisted",
                             test_itemzflow_is_whitelisted);
  failures += onion_test_run("app_jailbreak.existing_policy",
                             test_existing_policy_is_preserved);
  failures += onion_test_run("app_jailbreak.configured_policy",
                             test_configured_policy_replaces_defaults);
  return failures;
}
