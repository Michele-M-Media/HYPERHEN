/* Host tests: IPC wire helpers (frame, escape, reply body, null path). */
#include "test_harness.h"

#include <msg.hpp>
#include <onion/ipc_server.hpp>

#include <cstring>
#include <string>

static int test_frame_complete(void) {
  TEST_ASSERT_TRUE(onion::ipc_frame_is_complete(static_cast<int>(sizeof(IPCMessage))));
  TEST_ASSERT_TRUE(!onion::ipc_frame_is_complete(0));
  TEST_ASSERT_TRUE(!onion::ipc_frame_is_complete(4));
  TEST_ASSERT_TRUE(
      !onion::ipc_frame_is_complete(static_cast<int>(sizeof(IPCMessage)) - 1));
  return 0;
}

static int test_force_nul(void) {
  IPCMessage m{};
  memset(m.msg, 'A', sizeof(m.msg));
  onion::ipc_message_force_nul(m);
  TEST_ASSERT_EQ_INT(0, m.msg[sizeof(m.msg) - 1]);
  return 0;
}

static int test_json_escape_quotes_and_slash(void) {
  std::string e = onion::ipc_json_escape(R"(a"b\c)");
  TEST_ASSERT_STREQ(R"(a\"b\\c)", e.c_str());
  std::string nl = onion::ipc_json_escape("x\ny");
  TEST_ASSERT_STREQ(R"(x\ny)", nl.c_str());
  return 0;
}

static int test_format_reply_escapes_var(void) {
  using onion::ipc_format_reply_body;
  std::string ok = ipc_format_reply_body(false, "Nothing");
  TEST_ASSERT_STREQ(R"({"res":0,"var":"Nothing"})", ok.c_str());

  std::string err = ipc_format_reply_body(true, "fail");
  TEST_ASSERT_STREQ(R"({"res":-1,"var":"fail"})", err.c_str());

  std::string quoted = ipc_format_reply_body(false, R"(say "hi")");
  TEST_ASSERT_TRUE(quoted.find(R"(\"hi\")") != std::string::npos);
  TEST_ASSERT_TRUE(quoted.find("\"var\":\"say") != std::string::npos);
  /* Must be parseable as JSON-ish: no raw unescaped quote inside var value. */
  TEST_ASSERT_TRUE(quoted.find(R"("var":"say \"hi\"")") != std::string::npos);
  return 0;
}

static int test_null_path_policy(void) {
  /* Mirror daemon handleIPC guards: missing path is hard fail. */
  auto missing = [](const char *p) -> bool { return !p || !*p; };
  TEST_ASSERT_TRUE(missing(nullptr));
  TEST_ASSERT_TRUE(missing(""));
  TEST_ASSERT_TRUE(!missing("/data/HyperHEN"));
  return 0;
}

extern "C" int test_ipc_harden_suite(void) {
  int failures = 0;
  failures += onion_test_run("ipc_frame_complete", test_frame_complete);
  failures += onion_test_run("ipc_force_nul", test_force_nul);
  failures += onion_test_run("ipc_json_escape", test_json_escape_quotes_and_slash);
  failures += onion_test_run("ipc_format_reply_escape", test_format_reply_escapes_var);
  failures += onion_test_run("ipc_null_path_policy", test_null_path_policy);
  return failures;
}
