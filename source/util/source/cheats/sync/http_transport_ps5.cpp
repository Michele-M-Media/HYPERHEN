#include "cheats/sync/http_transport_ps5.hpp"

#include <onion/log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <strings.h>

#if !defined(ONION_HOST_TEST)
#include <curl/curl.h>

#include <functional>

extern "C" {
__asm__(
    ".pushsection .rodata\n"
    ".balign 1\n"
    ".global onion_curl_ca_bundle_start\n"
    "onion_curl_ca_bundle_start:\n"
    ".incbin \"" ONION_CURL_CA_BUNDLE "\"\n"
    ".global onion_curl_ca_bundle_end\n"
    "onion_curl_ca_bundle_end:\n"
    ".popsection\n");
extern const unsigned char onion_curl_ca_bundle_start[];
extern const unsigned char onion_curl_ca_bundle_end[];
}
#endif

namespace onion::cheats::sync {
namespace {

const char *scheme_host(const char *url) {
  if (!url) {
    return nullptr;
  }
  if (std::strncmp(url, "https://", 8) == 0) {
    return url + 8;
  }
  if (std::strncmp(url, "http://", 7) == 0) {
    return url + 7;
  }
  return nullptr;
}

bool host_allowed(const char *url, const char *allow) {
  const char *host = scheme_host(url);
  if (!host) {
    return false;
  }
  if (!allow || !allow[0]) {
    return true;
  }
  const char *slash = std::strchr(host, '/');
  const char *colon = std::strchr(host, ':');
  size_t host_len = std::strlen(host);
  if (colon && (!slash || colon < slash)) {
    host_len = static_cast<size_t>(colon - host);
  } else if (slash) {
    host_len = static_cast<size_t>(slash - host);
  }
  const size_t allow_len = std::strlen(allow);
  if (host_len == allow_len && std::strncmp(host, allow, allow_len) == 0) {
    return true;
  }
  if (host_len > allow_len + 1 && host[host_len - allow_len - 1] == '.' &&
      std::strncmp(host + host_len - allow_len, allow, allow_len) == 0) {
    return true;
  }
  return false;
}

#if !defined(ONION_HOST_TEST)
// OpenSSL X509 verify codes surfaced via CURLINFO_SSL_VERIFYRESULT.
constexpr long kX509CertNotYetValid = 9;  // X509_V_ERR_CERT_NOT_YET_VALID
constexpr long kX509CertHasExpired = 10;  // X509_V_ERR_CERT_HAS_EXPIRED

extern "C" int sceNetInit(void);

struct CurlXfer {
  const std::function<SyncStatus(const void *, size_t)> *on_data = nullptr;
  HttpProgressFn on_progress = nullptr;
  void *progress_user = nullptr;
  SyncCancelFn should_cancel = nullptr;
  void *cancel_user = nullptr;
  SyncStatus st = SyncStatus::Ok;
  size_t bytes = 0;
  size_t max_body_bytes = 0;
  size_t last_progress_bytes = 0;
  int last_progress_percent = -2;
  uint64_t resume_offset = 0;
  bool *range_ignored = nullptr;
  long response_code = 0;
  bool content_range_ok = false;
};

extern "C" size_t onion_curl_header(char *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
  if (size && nmemb > std::numeric_limits<size_t>::max() / size) return 0;
  const size_t n = size * nmemb;
  auto *xfer = static_cast<CurlXfer *>(userdata);
  if (!xfer || !n) return n;
  const std::string line(ptr, n);
  if (line.rfind("HTTP/", 0) == 0) {
    const size_t space = line.find(' ');
    xfer->response_code = space == std::string::npos ? 0 :
        std::strtol(line.c_str() + space + 1, nullptr, 10);
    xfer->content_range_ok = false;
  } else if (xfer->resume_offset &&
             strncasecmp(line.c_str(), "Content-Range:", 14) == 0) {
    unsigned long long start = 0, end = 0, total = 0;
    char extra = 0;
    const int fields = std::sscanf(line.c_str() + 14, " bytes %llu-%llu/%llu %c",
                                   &start, &end, &total, &extra);
    xfer->content_range_ok = fields == 3 && start == xfer->resume_offset &&
                             end >= start && total > end;
  }
  return n;
}

size_t curl_size(curl_off_t value) {
  if (value <= 0) {
    return 0;
  }
  return static_cast<size_t>(value);
}

extern "C" int onion_curl_xferinfo(void *userdata, curl_off_t download_total,
                                    curl_off_t download_now, curl_off_t,
                                    curl_off_t) {
  auto *xfer = static_cast<CurlXfer *>(userdata);
  if (!xfer) {
    return 0;
  }
  if (xfer->should_cancel && xfer->should_cancel(xfer->cancel_user)) {
    xfer->st = SyncStatus::Cancelled;
    return 1;
  }
  if (!xfer->on_progress)
    return 0;

  const size_t received = curl_size(download_now);
  const size_t total = curl_size(download_total);
  const int percent = total > 0
                          ? static_cast<int>(std::min<size_t>(
                                100, (received * 100) / total))
                          : -1;
  const bool report_percent =
      percent >= 0 && percent != xfer->last_progress_percent;
  const bool report_bytes =
      percent < 0 &&
      (xfer->last_progress_percent == -2 ||
       received >= xfer->last_progress_bytes + 1024 * 1024);
  if (report_percent || report_bytes) {
    xfer->last_progress_percent = percent;
    xfer->last_progress_bytes = received;
    xfer->on_progress(received, total, xfer->progress_user);
  }
  return 0;
}

extern "C" size_t onion_curl_write(char *ptr, size_t size, size_t nmemb,
                                   void *userdata) {
  auto *xfer = static_cast<CurlXfer *>(userdata);
  if (size && nmemb > std::numeric_limits<size_t>::max() / size) return 0;
  const size_t n = size * nmemb;
  if (!xfer) {
    return n;
  }
  // Redirect/error response bodies must never become model bytes.
  if (xfer->response_code < 200 || xfer->response_code > 299) return n;
  if (xfer->resume_offset &&
      (xfer->response_code != 206 || !xfer->content_range_ok)) {
    if (xfer->response_code == 200 && xfer->range_ignored)
      *xfer->range_ignored = true;
    xfer->st = SyncStatus::Protocol;
    return 0;
  }
  if (xfer->max_body_bytes != 0 &&
      (xfer->bytes > xfer->max_body_bytes ||
       n > xfer->max_body_bytes - xfer->bytes)) {
    xfer->st = SyncStatus::Protocol;
    return 0;
  }
  xfer->bytes += n;
  if (xfer->on_data && n > 0) {
    xfer->st = (*xfer->on_data)(ptr, n);
    if (xfer->st != SyncStatus::Ok) {
      return 0;
    }
  }
  return n;
}

extern "C" int onion_curl_debug(CURL *, curl_infotype type, char *data,
                                size_t size, void *) {
  const char *kind = nullptr;
  switch (type) {
  case CURLINFO_TEXT:
    kind = "text";
    break;
  case CURLINFO_HEADER_IN:
    kind = "hdr-in";
    break;
  case CURLINFO_HEADER_OUT:
    kind = "hdr-out";
    break;
  default:
    return 0;
  }
  while (size > 0 && (data[size - 1] == '\n' || data[size - 1] == '\r')) {
    --size;
  }
  if (size == 0) {
    return 0;
  }
  if (size > 240) {
    size = 240;
  }
  LOG_DEBUG("curl %s %.*s", kind, static_cast<int>(size), data);
  return 0;
}

bool ensure_curl() {
  static std::once_flag once;
  static bool ready = false;
  std::call_once(once, [] {
  const int net = sceNetInit();
  LOG_DEBUG("http curl sceNetInit rc=%d", net);

  const CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (rc != CURLE_OK) {
    LOG_ERROR("http curl_global_init failed: %s (%d)", curl_easy_strerror(rc),
              static_cast<int>(rc));
    return;
  }
  LOG_DEBUG("http curl ready version=%s", curl_version());
  const size_t ca_bundle_size = static_cast<size_t>(
      reinterpret_cast<uintptr_t>(onion_curl_ca_bundle_end) -
      reinterpret_cast<uintptr_t>(onion_curl_ca_bundle_start));
  LOG_DEBUG("http curl ca bundle embedded bytes=%zu verify=peer+host",
            ca_bundle_size);
    ready = true;
  });
  return ready;
}
#endif

} // namespace

SyncStatus Ps5HttpTransport::perform(
    const HttpRequest &req,
    const std::function<SyncStatus(const void *, size_t)> &on_data) {
  const char *url = req.url ? req.url : "";
  const char *method = req.method ? req.method : "GET";
  const char *allow = req.host_allow ? req.host_allow : "";
  if (req.range_ignored) *req.range_ignored = false;
  LOG_DEBUG("http perform method=%s url=%s host_allow=%s timeout_ms=%d "
            "status=%d-%d",
            method, url, allow, req.timeout_ms, req.status_min,
            req.status_max);

  if ((req.https_only && std::strncmp(url, "https://", 8) != 0) ||
      !host_allowed(req.url, req.host_allow)) {
    LOG_ERROR("http url rejected url=%s host_allow=%s", url, allow);
    return SyncStatus::Rejected;
  }
#if defined(ONION_HOST_TEST)
  (void)on_data;
  return SyncStatus::Unavailable;
#else
  if (!ensure_curl()) {
    return SyncStatus::Unavailable;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    LOG_ERROR("http curl_easy_init failed");
    return SyncStatus::Unavailable;
  }

  CurlXfer xfer;
  xfer.on_data = on_data ? &on_data : nullptr;
  xfer.on_progress = req.on_progress;
  xfer.progress_user = req.progress_user;
  xfer.should_cancel = req.should_cancel;
  xfer.cancel_user = req.cancel_user;
  xfer.max_body_bytes = req.max_body_bytes;
  xfer.resume_offset = req.resume_from_bytes;
  xfer.range_ignored = req.range_ignored;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   req.user_agent && req.user_agent[0] ? req.user_agent
                                                       : "HyperHEN");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
  if (req.https_only) {
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  }
  if (req.resume_from_bytes)
    curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                     static_cast<curl_off_t>(req.resume_from_bytes));
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  const size_t ca_bundle_size = static_cast<size_t>(
      reinterpret_cast<uintptr_t>(onion_curl_ca_bundle_end) -
      reinterpret_cast<uintptr_t>(onion_curl_ca_bundle_start));
  struct curl_blob ca_bundle {
    const_cast<unsigned char *>(onion_curl_ca_bundle_start), ca_bundle_size,
        CURL_BLOB_NOCOPY
  };
  curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca_bundle);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onion_curl_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &xfer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onion_curl_header);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &xfer);
  if (req.on_progress || req.should_cancel) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onion_curl_xferinfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &xfer);
  }
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   req.timeout_ms > 0 && req.timeout_ms < 30000
                       ? static_cast<long>(req.timeout_ms)
                       : 30000L);
  if (req.timeout_ms > 0) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout_ms));
  }
  if (std::strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body ? req.body : "");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(req.body_len));
  }
  struct curl_slist *headers = nullptr;
  if (req.content_type && req.content_type[0]) {
    std::string line = "Content-Type: ";
    line += req.content_type;
    headers = curl_slist_append(headers, line.c_str());
  }
  if (req.accept && req.accept[0]) {
    std::string line = "Accept: ";
    line += req.accept;
    headers = curl_slist_append(headers, line.c_str());
  }
  if (headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  if (onion_log_runtime_level >= ONION_LOG_DEBUG) {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, onion_curl_debug);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long http_code = 0;
  char *primary_ip = nullptr;
  double namelookup = 0;
  double connect = 0;
  double total = 0;
  long ssl_verify = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (req.resume_from_bytes && http_code == 200 && req.range_ignored)
    *req.range_ignored = true;
  curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &primary_ip);
  curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &namelookup);
  curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect);
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
  if (rc == CURLE_PEER_FAILED_VERIFICATION) {
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify);
  }

  LOG_DEBUG("http result url=%s curl=%s(%d) http=%ld ip=%s dns=%.3fs "
            "connect=%.3fs total=%.3fs bytes=%zu xfer=%s",
            url, curl_easy_strerror(rc), static_cast<int>(rc), http_code,
            primary_ip && primary_ip[0] ? primary_ip : "-", namelookup,
            connect, total, xfer.bytes, sync_status_name(xfer.st));

  if (headers) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl);

  if (xfer.st != SyncStatus::Ok) {
    return xfer.st;
  }
  if (rc != CURLE_OK) {
    if (rc == CURLE_PEER_FAILED_VERIFICATION) {
      LOG_ERROR("http tls verify failed verify=%ld url=%s", ssl_verify, url);
      if (ssl_verify == kX509CertNotYetValid ||
          ssl_verify == kX509CertHasExpired) {
        return SyncStatus::Clock;
      }
      return SyncStatus::Tls;
    }
    return SyncStatus::Network;
  }
  const int min_code = req.status_min > 0 ? req.status_min : 200;
  const int max_code = req.status_max > 0 ? req.status_max : 299;
  if (http_code < min_code || http_code > max_code) {
    LOG_ERROR("http status out of range code=%ld want=%d-%d url=%s", http_code,
              min_code, max_code, url);
    return SyncStatus::Network;
  }
  return SyncStatus::Ok;
#endif
}

} // namespace onion::cheats::sync
