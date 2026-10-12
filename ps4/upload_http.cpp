// PShare PS4: native HTTP/TLS transport. SPDX-License-Identifier: GPL-3.0-or-later
#include "http_body.hpp"
#include "app/thread.hpp"
#include <atomic>
#include <unistd.h>
#include "pshare/core/text.hpp"
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <algorithm>
#include <mutex>
#include <ctime>
#include <new>
// OpenOrbis 0.5.4 leaves these prototypes untyped. Use ABI-correct aliases.
extern "C" int pshare_http_recv_timeout(int, std::uint32_t) __asm__("sceHttpSetRecvTimeOut");
extern "C" int pshare_ssl_term(int) __asm__("sceSslTerm");
namespace pshare {
namespace {
std::mutex http_mutex;
int http_id = -1, ssl_id = -1, pool_id = -1;
int init_step_error = 0;          // first failing init call, for diagnostics
const char *init_step = "not started";
// Verification stays on. This callback only records why the platform rejected
// a certificate, then keeps the rejection (a negative return aborts).
int32_t record_tls_error(int32_t, uint32_t verify_error, void *const[], int32_t, void *slot) {
    if (slot) *static_cast<uint32_t *>(slot) = verify_error;
    return verify_error ? -1 : 0;
}
struct CancelWatch {
    std::atomic<bool> done{false};
    app::Thread worker;
    CancelWatch(int req, UploadProgress *progress) {
        if (progress) worker.start([this,req,progress] {
            while (!done.load()) {
                if (progress->cancel.load()) { sceHttpAbortRequest(req); break; }
                usleep(50000);
            }
        });
    }
    ~CancelWatch() { done=true; worker.join(); }
};
struct Session {
    int tmpl = -1, conn = -1, req = -1;
    ~Session() {
        if (req >= 0) sceHttpDeleteRequest(req);
        if (conn >= 0) sceHttpDeleteConnection(conn);
        if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    }
};
std::string failed(const char *operation, int code) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08x)", operation, static_cast<unsigned>(code));
    return buffer;
}
}
void upload_global_init() {
    std::lock_guard<std::mutex> lock(http_mutex);
    if (http_id >= 0) return;
    pool_id = sceNetPoolCreate("pshare-http", 4u << 20, 0);
    if (pool_id < 0) { init_step = "sceNetPoolCreate"; init_step_error = pool_id; return; }
    ssl_id = sceSslInit(4u << 20);
    if (ssl_id < 0) { init_step = "sceSslInit"; init_step_error = ssl_id; sceNetPoolDestroy(pool_id); pool_id = -1; return; }
    http_id = sceHttpInit(pool_id, ssl_id, 4u << 20);
    if (http_id < 0) { init_step = "sceHttpInit"; init_step_error = http_id; pshare_ssl_term(ssl_id); sceNetPoolDestroy(pool_id); ssl_id = pool_id = -1; return; }
    init_step = "ready";
}
std::string upload_transport_report() {
    upload_global_init();
    std::lock_guard<std::mutex> lock(http_mutex);
    if (http_id >= 0) return "native sceHttp/sceSsl ready (TLS verification on)";
    return std::string("native sceHttp unavailable: ") + failed(init_step, init_step_error);
}
HttpResult perform_request(const HttpRequest &request, UploadProgress *progress, int timeout_seconds) {
    upload_global_init();
    std::lock_guard<std::mutex> lock(http_mutex);
    HttpResult result;
    if (http_id < 0) { result.error = "PS4 HTTP initialization failed: " + failed(init_step, init_step_error); return result; }
    std::string body, content_type;
    const std::string boundary = "PSharePS4Boundary" + std::to_string(std::time(nullptr));
    try {
        if (!ps4::make_body(request, boundary, body, content_type, result.error, progress)) return result;
    } catch (const std::bad_alloc &) {
        result.error = "Not enough memory to buffer this upload; use a USB/folder destination";
        return result;
    }
    if (progress && progress->cancel.load()) { result.error = "Cancelled"; return result; }
    Session s;
    // 1: honour the console's own proxy settings, as the OpenOrbis sample does.
    s.tmpl = sceHttpCreateTemplate(http_id, "PShare-PS4/1.0", ORBIS_HTTP_VERSION_1_1, 1);
    if (s.tmpl < 0) { result.error = failed("HTTP template", s.tmpl); return result; }
    uint32_t tls_error = 0;
    sceHttpsSetSslCallback(s.tmpl, record_tls_error, &tls_error);
    // Keep the platform's TLS peer/hostname verification enabled.
    const auto timeout = static_cast<std::uint32_t>(std::clamp(timeout_seconds, 1, 600)) * 1000000u;
    sceHttpSetConnectTimeOut(s.tmpl, 15000000);
    sceHttpSetResolveTimeOut(s.tmpl, 15000000);
    sceHttpSetSendTimeOut(s.tmpl, timeout);
    pshare_http_recv_timeout(s.tmpl, timeout);
    s.conn = sceHttpCreateConnectionWithURL(s.tmpl, request.url.c_str(), false);
    if (s.conn < 0) { result.error = failed("HTTP connection", s.conn); return result; }
    s.req = sceHttpCreateRequestWithURL(s.conn, request.body == BodyKind::none ? ORBIS_METHOD_GET : ORBIS_METHOD_POST,
                                      request.url.c_str(), body.size());
    if (s.req < 0) { result.error = failed("HTTP request", s.req); return result; }
    if (!content_type.empty()) sceHttpAddRequestHeader(s.req, "Content-Type", content_type.c_str(), 0);
    for (const auto &header : request.headers) {
        const auto colon = header.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = std::string(trim(std::string_view(header).substr(0, colon)));
        const std::string value = std::string(trim(std::string_view(header).substr(colon + 1)));
        const int rc = sceHttpAddRequestHeader(s.req, key.c_str(), value.c_str(), 0);
        if (rc < 0) { result.error = failed("HTTP header", rc); return result; }
    }
    if (progress) { progress->sent = 0; progress->total = request.upload_size; }
    CancelWatch cancellation(s.req, progress);
    int rc = sceHttpSendRequest(s.req, body.empty() ? nullptr : body.data(), body.size());
    if (rc < 0) {
        result.error = failed("HTTP send/TLS", rc);
        if (tls_error) { char extra[64]; std::snprintf(extra, sizeof extra, "; certificate rejected (flags 0x%x)", tls_error); result.error += extra; }
        return result;
    }
    int status = 0;
    rc = sceHttpGetStatusCode(s.req, &status);
    if (rc < 0) { result.error = failed("HTTP status", rc); return result; }
    result.status = status;
    char buffer[16384];
    for (;;) {
        if (progress && progress->cancel.load()) { sceHttpAbortRequest(s.req); result.error = "Cancelled"; return result; }
        rc = sceHttpReadData(s.req, buffer, sizeof(buffer));
        if (rc < 0) { result.error = failed("HTTP response", rc); return result; }
        if (rc == 0) break;
        if (result.body.size() + rc > (256u << 10)) { result.error = "Response too large"; return result; }
        result.body.append(buffer, rc);
    }
    result.transport_ok = true;
    if (progress) progress->sent = request.upload_size;
    return result;
}
}
