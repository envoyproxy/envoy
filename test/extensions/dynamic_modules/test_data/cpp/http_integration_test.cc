#include <algorithm>
#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk.h"

namespace Envoy {
namespace DynamicModules {

// Helper assertions
void assertEq(const std::string& a, const std::string& b, const std::string& msg) {
  if (a != b) {
    // In a real environment, we might want to log or throw, but here we just assert/abort if
    // possible. Since this is loaded as a shared object, specific test frameworks might catch
    // signals.
    assert(a == b);
  }
}

void assertEq(size_t a, size_t b, const std::string& msg) { assert(a == b); }

void assertTrue(bool cond, const std::string& msg) { assert(cond); }

// -----------------------------------------------------------------------------
// ConfigScheduler
// -----------------------------------------------------------------------------

class ConfigSchedulerFilter : public HttpFilter {
public:
  ConfigSchedulerFilter(std::shared_ptr<std::atomic<bool>> shared_status)
      : shared_status_(shared_status) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    if (shared_status_->load()) {
      headers.set("x-test-status", "true");
    } else {
      headers.set("x-test-status", "false");
    }
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

  void onStreamComplete() override {}

private:
  std::shared_ptr<std::atomic<bool>> shared_status_;
};

class ConfigSchedulerFilterFactory : public HttpFilterFactory {
public:
  ConfigSchedulerFilterFactory(std::shared_ptr<std::atomic<bool>> shared_status)
      : shared_status_(shared_status) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<ConfigSchedulerFilter>(shared_status_);
  }

private:
  std::shared_ptr<std::atomic<bool>> shared_status_;
};

class ConfigSchedulerConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    auto shared_status = std::make_shared<std::atomic<bool>>(false);

    // Simulate async config update.
    std::thread([shared_status]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // NO_CHECK_FORMAT(real_time)
      shared_status->store(true);
    }).detach();

    return std::make_unique<ConfigSchedulerFilterFactory>(shared_status);
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(ConfigSchedulerConfigFactory, "http_config_scheduler");

// -----------------------------------------------------------------------------
// Passthrough
// -----------------------------------------------------------------------------

class PassthroughFilter : public HttpFilter {
public:
  PassthroughFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    DYM_LOG(handle_, LogLevel::Trace, "on_request_headers called");
    DYM_LOG(handle_, LogLevel::Debug, "on_request_headers called");
    DYM_LOG(handle_, LogLevel::Info, "on_request_headers called");
    DYM_LOG(handle_, LogLevel::Warn, "on_request_headers called");
    DYM_LOG(handle_, LogLevel::Error, "on_request_headers called");
    DYM_LOG(handle_, LogLevel::Critical, "on_request_headers called");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
};

class PassthroughFilterFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    DYM_LOG(handle, LogLevel::Trace, "new_http_filter called");
    DYM_LOG(handle, LogLevel::Debug, "new_http_filter called");
    DYM_LOG(handle, LogLevel::Info, "new_http_filter called");
    DYM_LOG(handle, LogLevel::Warn, "new_http_filter called");
    DYM_LOG(handle, LogLevel::Error, "new_http_filter called");
    DYM_LOG(handle, LogLevel::Critical, "new_http_filter called");
    return std::make_unique<PassthroughFilter>(handle);
  }
};

class PassthroughConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<PassthroughFilterFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(PassthroughConfigFactory, "passthrough");

// -----------------------------------------------------------------------------
// HeaderCallbacks
// -----------------------------------------------------------------------------

class HeaderCallbacksFilter : public HttpFilter {
public:
  HeaderCallbacksFilter(const std::map<std::string, std::string>& headers_to_add)
      : headers_to_add_(headers_to_add) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    req_headers_called_ = true;
    assertEq(std::string(headers.getOne(":path")), "/test/long/url", ":path header");
    assertEq(std::string(headers.getOne(":method")), "POST", ":method header");
    assertEq(std::string(headers.getOne("foo")), "bar", "foo header");

    for (const auto& [k, v] : headers_to_add_) {
      headers.set(k, v);
    }

    // Test setter/getter
    headers.set("new", "value1");
    assertEq(std::string(headers.getOne("new")), "value1", "new header set");

    auto vals = headers.get("new");
    assertEq(vals.size(), 1, "new header count");
    assertEq(std::string(vals[0]), "value1", "new header val");

    // Test add
    headers.add("new", "value2");
    vals = headers.get("new");
    assertEq(vals.size(), 2, "new header count after add");
    assertEq(std::string(vals[1]), "value2", "new header val 2");

    // Test remove
    headers.remove("new");
    assertEq(std::string(headers.getOne("new")), "", "new header removed");
    return HeadersStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    req_trailers_called_ = true;
    assertEq(std::string(trailers.getOne("foo")), "bar", "foo trailer");
    for (const auto& [k, v] : headers_to_add_) {
      trailers.set(k, v);
    }
    // Test setter/getter
    trailers.set("new", "value1");
    assertEq(std::string(trailers.getOne("new")), "value1", "new trailer set");

    // Test add
    trailers.add("new", "value2");
    auto vals = trailers.get("new");
    assertEq(vals.size(), 2, "new trailer count");

    // Test remove
    trailers.remove("new");
    assertEq(std::string(trailers.getOne("new")), "", "new trailer removed");
    return TrailersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    res_headers_called_ = true;
    assertEq(std::string(headers.getOne("foo")), "bar", "foo response header");
    for (const auto& [k, v] : headers_to_add_) {
      headers.set(k, v);
    }

    headers.set("new", "value1");
    assertEq(std::string(headers.getOne("new")), "value1", "new resp header");
    headers.add("new", "value2");
    assertEq(headers.get("new").size(), 2, "new resp header count");
    headers.remove("new");
    assertEq(std::string(headers.getOne("new")), "", "new resp header removed");
    return HeadersStatus::Continue;
  }

  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    res_trailers_called_ = true;
    assertEq(std::string(trailers.getOne("foo")), "bar", "foo response trailer");
    for (const auto& [k, v] : headers_to_add_) {
      trailers.set(k, v);
    }

    trailers.set("new", "value1");
    assertEq(std::string(trailers.getOne("new")), "value1", "new resp trailer");
    trailers.add("new", "value2");
    assertEq(trailers.get("new").size(), 2, "new resp trailer count");
    trailers.remove("new");
    assertEq(std::string(trailers.getOne("new")), "", "new resp trailer removed");
    return TrailersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }

  void onStreamComplete() override {
    assertTrue(req_headers_called_, "reqHeadersCalled");
    assertTrue(req_trailers_called_, "reqTrailersCalled");
    assertTrue(res_headers_called_, "resHeadersCalled");
    assertTrue(res_trailers_called_, "resTrailersCalled");
  }

private:
  std::map<std::string, std::string> headers_to_add_;
  bool req_headers_called_{false};
  bool req_trailers_called_{false};
  bool res_headers_called_{false};
  bool res_trailers_called_{false};
};

class HeaderCallbacksFilterFactory : public HttpFilterFactory {
public:
  HeaderCallbacksFilterFactory(std::map<std::string, std::string> headers_to_add)
      : headers_to_add_(std::move(headers_to_add)) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HeaderCallbacksFilter>(headers_to_add_);
  }

private:
  std::map<std::string, std::string> headers_to_add_;
};

class HeaderCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    std::map<std::string, std::string> headers_to_add;
    if (!config_view.empty()) {
      std::string str(config_view);
      size_t pos = 0;
      while ((pos = str.find(',')) != std::string::npos) {
        std::string part = str.substr(0, pos);
        size_t sep = part.find(':');
        if (sep != std::string::npos) {
          headers_to_add[part.substr(0, sep)] = part.substr(sep + 1);
        }
        str.erase(0, pos + 1);
      }
      size_t sep = str.find(':');
      if (sep != std::string::npos) {
        headers_to_add[str.substr(0, sep)] = str.substr(sep + 1);
      }
    }
    return std::make_unique<HeaderCallbacksFilterFactory>(std::move(headers_to_add));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HeaderCallbacksConfigFactory, "header_callbacks");

// -----------------------------------------------------------------------------
// PerRoute
// -----------------------------------------------------------------------------

class PerRouteConfig : public RouteSpecificConfig {
public:
  PerRouteConfig(std::string value) : value_(std::move(value)) {}
  std::string value_;
};

class PerRouteFilter : public HttpFilter {
public:
  PerRouteFilter(HttpFilterHandle& handle, std::string value)
      : handle_(handle), value_(std::move(value)) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    headers.set("x-config", value_);

    const auto* cfg = handle_.getMostSpecificConfig();
    if (cfg != nullptr) {
      const auto* typed_cfg = dynamic_cast<const PerRouteConfig*>(cfg);
      if (typed_cfg) {
        per_route_config_ = typed_cfg->value_;
        headers.set("x-per-route-config", per_route_config_);
      }
    }

    return HeadersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    if (!per_route_config_.empty()) {
      headers.set("x-per-route-config-response", per_route_config_);
    }
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
  std::string value_;
  std::string per_route_config_;
};

class PerRouteFilterFactory : public HttpFilterFactory {
public:
  PerRouteFilterFactory(std::string value) : value_(std::move(value)) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<PerRouteFilter>(handle, value_);
  }

private:
  std::string value_;
};

class PerRouteConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<PerRouteFilterFactory>(std::string(config_view));
  }

  std::unique_ptr<RouteSpecificConfig> createPerRoute(absl::string_view config_view) override {
    return std::make_unique<PerRouteConfig>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(PerRouteConfigFactory, "per_route_config");

// -----------------------------------------------------------------------------
// BodyCallbacks
// -----------------------------------------------------------------------------

class BodyCallbacksFilter : public HttpFilter {
public:
  BodyCallbacksFilter(HttpFilterHandle& handle, bool immediate)
      : handle_(handle), immediate_(immediate) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Stop;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    if (!end_stream) {
      assertTrue(!immediate_, "immediate_end_of_stream is true but got !endOfStream");
      return BodyStatus::StopAndBuffer;
    }
    seen_request_body_ = true;

    std::string body_content;

    for (const auto& c : handle_.bufferedRequestBody().getChunks()) {
      body_content += c.toStringView();
    }
    for (const auto& c : body.getChunks()) {
      body_content += c.toStringView();
    }

    assertEq(body_content, "request_body", "request body content");

    // Drain everything
    handle_.bufferedRequestBody().drain(handle_.bufferedRequestBody().getSize());
    body.drain(body.getSize());

    // Append new
    body.append("new_request_body");
    handle_.requestHeaders().set("content-length", "16");

    return BodyStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Stop;
  }

  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    if (!end_stream) {
      return BodyStatus::StopAndBuffer;
    }
    seen_response_body_ = true;

    std::string body_content;

    for (const auto& c : handle_.bufferedResponseBody().getChunks()) {
      body_content += c.toStringView();
    }
    for (const auto& c : body.getChunks()) {
      body_content += c.toStringView();
    }

    assertEq(body_content, "response_body", "response body content");

    handle_.bufferedResponseBody().drain(handle_.bufferedResponseBody().getSize());
    body.drain(body.getSize());

    body.append("new_response_body");
    handle_.responseHeaders().set("content-length", "17");

    return BodyStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

  void onStreamComplete() override {
    assertTrue(seen_request_body_, "seenRequestBody");
    assertTrue(seen_response_body_, "seenResponseBody");
  }

private:
  HttpFilterHandle& handle_;
  bool immediate_;
  bool seen_request_body_{false};
  bool seen_response_body_{false};
};

class BodyCallbacksFilterFactory : public HttpFilterFactory {
public:
  BodyCallbacksFilterFactory(bool immediate) : immediate_(immediate) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<BodyCallbacksFilter>(handle, immediate_);
  }

private:
  bool immediate_;
};

class BodyCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    bool immediate = (config_view == "immediate_end_of_stream");
    return std::make_unique<BodyCallbacksFilterFactory>(immediate);
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(BodyCallbacksConfigFactory, "body_callbacks");

// -----------------------------------------------------------------------------
// SendResponse
// -----------------------------------------------------------------------------

class SendResponseFilter : public HttpFilter {
public:
  SendResponseFilter(HttpFilterHandle& handle, std::string mode) : handle_(handle), mode_(mode) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    if (mode_ == "on_request_headers") {
      std::vector<HeaderView> h = {{"some_header", "some_value"}};
      handle_.sendLocalResponse(200, h, "local_response_body_from_on_request_headers",
                                "test_details");
      return HeadersStatus::StopAllAndBuffer;
    }
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    if (mode_ == "on_request_body") {
      std::vector<HeaderView> h = {{"some_header", "some_value"}};
      handle_.sendLocalResponse(200, h, "local_response_body_from_on_request_body", "");
      return BodyStatus::StopAndBuffer;
    }
    return BodyStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    if (mode_ == "on_response_headers") {
      std::vector<HeaderView> h = {{"some_header", "some_value"}};
      handle_.sendLocalResponse(500, h, "local_response_body_from_on_response_headers", "");
      return HeadersStatus::StopAllAndBuffer;
    }
    return HeadersStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
  std::string mode_;
};

class SendResponseFilterFactory : public HttpFilterFactory {
public:
  SendResponseFilterFactory(std::string mode) : mode_(std::move(mode)) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<SendResponseFilter>(handle, mode_);
  }

private:
  std::string mode_;
};

class SendResponseConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<SendResponseFilterFactory>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(SendResponseConfigFactory, "send_response");

// -----------------------------------------------------------------------------
// HttpCallouts
// -----------------------------------------------------------------------------

class HttpCalloutsFilter : public HttpFilter, public HttpCalloutCallback {
public:
  HttpCalloutsFilter(HttpFilterHandle& handle, std::string cluster_name)
      : handle_(handle), cluster_name_(std::move(cluster_name)) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    std::vector<HeaderView> callout_headers = {
        {":path", "/"}, {":method", "GET"}, {"host", "example.com"}};
    auto result = handle_.httpCallout(cluster_name_, callout_headers, "http_callout_body", 1000,
                                      *this); // this callback needs to outlive the callout?
                                              // SDK says cb must remain valid. Since we are the
                                              // plugin and we stop, we should be fine.
    if (result.first != HttpCalloutInitResult::Success) {
      std::vector<HeaderView> h = {{"foo", "bar"}};
      handle_.sendLocalResponse(500, h, "", "");
    }
    callout_handle_ = result.second;
    return HeadersStatus::Stop;
  }

  void onHttpCalloutDone(HttpCalloutResult result, absl::Span<const HeaderView> headers,
                         absl::Span<const BufferView> body_chunks) override {
    if (cluster_name_ == "resetting_cluster") {
      assertTrue(result == HttpCalloutResult::Reset, "expected reset");
      return;
    }
    assertTrue(result == HttpCalloutResult::Success, "callout success");

    bool found = false;
    for (const auto& h : headers) {
      if (h.key() == "some_header" && h.value() == "some_value") {
        found = true;
        break;
      }
    }
    assertTrue(found, "some_header found");

    std::string full_body;
    for (const auto& b : body_chunks) {
      full_body += b.toStringView();
    }
    assertEq(full_body, "response_body_from_callout", "resp body");

    std::vector<HeaderView> h = {{"some_header", "some_value"}};
    handle_.sendLocalResponse(200, h, "local_response_body", "callout_success");
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
  std::string cluster_name_;
  uint64_t callout_handle_{0};
};

class HttpCalloutsFilterFactory : public HttpFilterFactory {
public:
  HttpCalloutsFilterFactory(std::string cluster_name) : cluster_name_(std::move(cluster_name)) {}

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HttpCalloutsFilter>(handle, cluster_name_);
  }

private:
  std::string cluster_name_;
};

class HttpCalloutsConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<HttpCalloutsFilterFactory>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HttpCalloutsConfigFactory, "http_callouts");

// -----------------------------------------------------------------------------
// HttpFilterScheduler
// -----------------------------------------------------------------------------

class HttpFilterSchedulerFilter : public HttpFilter {
public:
  HttpFilterSchedulerFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    auto sched = handle_.getScheduler();
    sched->schedule([this, sched]() {
      event_ids_.push_back(0);
      // Event 1 needs to continue decoding
      sched->schedule([this]() {
        event_ids_.push_back(1);
        handle_.continueRequest();
      });
    });
    return HeadersStatus::StopAllAndBuffer;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    auto sched = handle_.getScheduler();
    sched->schedule([this, sched]() {
      event_ids_.push_back(2);
      sched->schedule([this]() {
        event_ids_.push_back(3);
        handle_.continueResponse();
      });
    });
    return HeadersStatus::StopAllAndBuffer;
  }

  void onStreamComplete() override {
    assertEq(event_ids_.size(), 4, "event count");
    for (size_t i = 0; i < event_ids_.size(); ++i) {
      assertEq(event_ids_[i], i, "event id order");
    }
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

private:
  HttpFilterHandle& handle_;
  std::vector<uint64_t> event_ids_;
};

class HttpFilterSchedulerFilterFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HttpFilterSchedulerFilter>(handle);
  }
};

class HttpFilterSchedulerConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<HttpFilterSchedulerFilterFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HttpFilterSchedulerConfigFactory, "http_filter_scheduler");

// -----------------------------------------------------------------------------
// FakeExternalCache
// -----------------------------------------------------------------------------

class FakeExternalCacheFilter : public HttpFilter {
public:
  FakeExternalCacheFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    std::string key(headers.getOne("cacahe-key"));
    auto sched = handle_.getScheduler();

    if (key == "existing") {
      sched->schedule([this]() {
        std::vector<HeaderView> h = {{"cached", "yes"}};
        handle_.sendLocalResponse(200, h, "cached_response_body", "");
      });
    } else {
      sched->schedule([this]() {
        handle_.requestHeaders().set("on-scheduled", "req");
        handle_.continueRequest();
      });
    }
    return HeadersStatus::Stop;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    auto sched = handle_.getScheduler();
    sched->schedule([this]() {
      handle_.responseHeaders().set("on-scheduled", "res");
      handle_.continueResponse();
    });
    return HeadersStatus::Stop;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
};

class FakeExternalCacheFilterFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<FakeExternalCacheFilter>(handle);
  }
};

class FakeExternalCacheConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<FakeExternalCacheFilterFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(FakeExternalCacheConfigFactory, "fake_external_cache");

// -----------------------------------------------------------------------------
// StatsCallbacks
// -----------------------------------------------------------------------------

struct StatsCallbacksIDs {
  MetricID reqTotal;
  MetricID reqPending;
  MetricID reqSet;
  MetricID reqVals;
  MetricID epTotal;
  MetricID epPending;
  MetricID epSet;
  MetricID epVals;
  std::string headerToCount;
  std::string headerToSet;
};

class StatsCallbacksFilter : public HttpFilter {
public:
  StatsCallbacksFilter(HttpFilterHandle& handle, StatsCallbacksIDs ids)
      : handle_(handle), ids_(ids) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    handle_.incrementCounterValue(ids_.reqTotal, 1);
    handle_.incrementGaugeValue(ids_.reqPending, 1);
    method_ = std::string(headers.getOne(":method"));
    std::vector<BufferView> tags = {{"on_request_headers"}, {method_}};

    handle_.incrementCounterValue(ids_.epTotal, 1, tags);
    handle_.incrementGaugeValue(ids_.epPending, 1, tags);

    std::string valStr(headers.getOne(ids_.headerToCount));
    if (!valStr.empty()) {
      try {
        uint64_t val = std::stoull(valStr);
        handle_.recordHistogramValue(ids_.reqVals, val);
        handle_.recordHistogramValue(ids_.epVals, val, tags);
      } catch (...) {
      }
    }

    valStr = std::string(headers.getOne(ids_.headerToSet));
    if (!valStr.empty()) {
      try {
        uint64_t val = std::stoull(valStr);
        handle_.setGaugeValue(ids_.reqSet, val);
        handle_.setGaugeValue(ids_.epSet, val, tags);
      } catch (...) {
      }
    }
    return HeadersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    std::vector<BufferView> req_tags = {{"on_request_headers"}, {method_}};
    std::vector<BufferView> res_tags = {{"on_response_headers"}, {method_}};

    handle_.incrementCounterValue(ids_.epTotal, 1, res_tags);
    handle_.decrementGaugeValue(ids_.reqPending, 1);
    handle_.decrementGaugeValue(ids_.epPending, 1, req_tags);
    handle_.incrementGaugeValue(ids_.epPending, 1, res_tags);

    std::string valStr(headers.getOne(ids_.headerToCount));
    if (!valStr.empty()) {
      try {
        uint64_t val = std::stoull(valStr);
        handle_.recordHistogramValue(ids_.epVals, val, res_tags);
      } catch (...) {
      }
    }
    valStr = std::string(headers.getOne(ids_.headerToSet));
    if (!valStr.empty()) {
      try {
        uint64_t val = std::stoull(valStr);
        handle_.setGaugeValue(ids_.epSet, val, res_tags);
      } catch (...) {
      }
    }
    return HeadersStatus::Continue;
  }

  void onStreamComplete() override {
    std::vector<BufferView> res_tags = {{"on_response_headers"}, {method_}};
    handle_.decrementGaugeValue(ids_.epPending, 1, res_tags);
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

private:
  HttpFilterHandle& handle_;
  StatsCallbacksIDs ids_;
  std::string method_;
};

class StatsCallbacksFilterFactory : public HttpFilterFactory {
public:
  StatsCallbacksFilterFactory(StatsCallbacksIDs ids) : ids_(ids) {}
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<StatsCallbacksFilter>(handle, ids_);
  }

private:
  StatsCallbacksIDs ids_;
};

class StatsCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    StatsCallbacksIDs ids;
    std::string cfg(config_view);
    size_t comma = cfg.find(',');
    if (comma != std::string::npos) {
      ids.headerToCount = cfg.substr(0, comma);
      ids.headerToSet = cfg.substr(comma + 1);
    }

    auto res = handle.defineCounter("requests_total");
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "c1");
    ids.reqTotal = res.first;

    res = handle.defineGauge("requests_pending");
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "g1");
    ids.reqPending = res.first;

    res = handle.defineGauge("requests_set_value");
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "g2");
    ids.reqSet = res.first;

    res = handle.defineHistogram("requests_header_values");
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "h1");
    ids.reqVals = res.first;

    std::vector<BufferView> tags = {{"entrypoint"}, {"method"}};
    res = handle.defineCounter("entrypoint_total", tags);
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "c2");
    ids.epTotal = res.first;

    res = handle.defineGauge("entrypoint_pending", tags);
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "g3");
    ids.epPending = res.first;

    res = handle.defineGauge("entrypoint_set_value", tags);
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "g4");
    ids.epSet = res.first;

    res = handle.defineHistogram("entrypoint_header_values", tags);
    assertEq((uint32_t)res.second, (uint32_t)MetricsResult::Success, "h2");
    ids.epVals = res.first;

    return std::make_unique<StatsCallbacksFilterFactory>(ids);
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(StatsCallbacksConfigFactory, "stats_callbacks");

// -----------------------------------------------------------------------------
// StreamingTerminal
// -----------------------------------------------------------------------------

class StreamingTerminalFilter : public HttpFilter, public DownstreamWatermarkCallbacks {
public:
  StreamingTerminalFilter(HttpFilterHandle& handle) : handle_(handle) {
    handle_.setDownstreamWatermarkCallbacks(*this);
  }

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    handle_.getScheduler()->schedule([this]() { onScheduledStartResponse(); });
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    if (end_stream) {
      request_closed_ = true;
    }
    handle_.getScheduler()->schedule([this]() { onScheduledReadRequest(); });
    return BodyStatus::StopAndBuffer;
  }

  void onScheduledStartResponse() {
    std::vector<HeaderView> headers = {
        {":status", "200"}, {"x-filter", "terminal"}, {"trailers", "x-status"}};
    handle_.sendResponseHeaders(headers, false);
    handle_.sendResponseData("Who are you?", false);
  }

  void onScheduledReadRequest() {
    if (!request_closed_) {
      auto& buf = handle_.bufferedRequestBody();
      buf.drain(buf.getSize());
      sendLargeResponseChunk();
    } else {
      handle_.sendResponseData("Thanks!", false);
      std::vector<HeaderView> trailers = {
          {"x-status", "finished"},
          {"x-above-watermark-count", std::to_string(above_w_)},
          {"x-below-watermark-count", std::to_string(below_w_)},
      };
      handle_.sendResponseTrailers(trailers);
    }
  }

  void sendLargeResponseChunk() {
    if (large_response_sent_ >= 8 * 1024) {
      return;
    }
    size_t size = 1024;
    std::string chunk(size, 'a');
    handle_.sendResponseData(chunk, false);
    large_response_sent_ += size;
  }

  void onAboveWriteBufferHighWatermark() override { above_w_++; }

  void onBelowWriteBufferLowWatermark() override {
    below_w_++;
    if (above_w_ == below_w_) {
      sendLargeResponseChunk();
    }
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }
  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
  bool request_closed_{false};
  int above_w_{0};
  int below_w_{0};
  int large_response_sent_{0};
};

class StreamingTerminalFilterFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<StreamingTerminalFilter>(handle);
  }
};

class StreamingTerminalConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<StreamingTerminalFilterFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(StreamingTerminalConfigFactory, "streaming_terminal_filter");

// -----------------------------------------------------------------------------
// HttpStreamBasic
// -----------------------------------------------------------------------------

class HttpStreamBasicFilter : public HttpFilter, public HttpStreamCallback {
public:
  HttpStreamBasicFilter(HttpFilterHandle& handle, std::string cluster)
      : handle_(handle), cluster_(std::move(cluster)) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    std::vector<HeaderView> h = {{":path", "/"}, {":method", "GET"}, {"host", "example.com"}};
    auto result = handle_.startHttpStream(cluster_, h, "", true, 5000, *this);
    if (result.first != HttpCalloutInitResult::Success) {
      std::vector<HeaderView> rh = {{"x-error", "stream_init_failed"}};
      handle_.sendLocalResponse(500, rh, "", "");
      return HeadersStatus::StopAllAndBuffer;
    }
    stream_id_ = result.second;
    bool success = handle_.sendHttpStreamData(stream_id_, "", true);
    assertTrue(success, "send basic data");
    return HeadersStatus::StopAllAndBuffer;
  }

  void onHttpStreamHeaders(uint64_t stream_id, absl::Span<const HeaderView> headers,
                           bool end_stream) override {
    assertEq(stream_id, stream_id_, "stream id");
    headers_received_ = true;
    bool found = false;
    for (const auto& h : headers) {
      if (h.key() == ":status" && h.value() == "200") {
        found = true;
      }
    }
    assertTrue(found, "status 200");
  }

  void onHttpStreamData(uint64_t stream_id, absl::Span<const BufferView> body,
                        bool end_stream) override {
    assertEq(stream_id, stream_id_, "stream id");
    data_received_ = true;
  }

  void onHttpStreamTrailers(uint64_t stream_id, absl::Span<const HeaderView> trailers) override {}

  void onHttpStreamComplete(uint64_t stream_id) override {
    assertEq(stream_id, stream_id_, "stream id");
    complete_ = true;
    std::vector<HeaderView> h = {{"x-stream-test", "basic"}};
    handle_.sendLocalResponse(200, h, "stream_callout_success", "");
  }

  void onHttpStreamReset(uint64_t stream_id, HttpStreamResetReason reason) override {}

  void onStreamComplete() override {
    assertTrue(headers_received_, "headers received");
    assertTrue(data_received_, "data received");
    assertTrue(complete_, "stream complete");
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }
  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

private:
  HttpFilterHandle& handle_;
  std::string cluster_;
  uint64_t stream_id_{0};
  bool headers_received_{false};
  bool data_received_{false};
  bool complete_{false};
};

class HttpStreamBasicFilterFactory : public HttpFilterFactory {
public:
  HttpStreamBasicFilterFactory(std::string cluster) : cluster_(std::move(cluster)) {}
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HttpStreamBasicFilter>(handle, cluster_);
  }

private:
  std::string cluster_;
};

class HttpStreamBasicConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<HttpStreamBasicFilterFactory>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HttpStreamBasicConfigFactory, "http_stream_basic");

// -----------------------------------------------------------------------------
// HttpStreamBidirectional
// -----------------------------------------------------------------------------

class HttpStreamBidiFilter : public HttpFilter, public HttpStreamCallback {
public:
  HttpStreamBidiFilter(HttpFilterHandle& handle, std::string cluster)
      : handle_(handle), cluster_(std::move(cluster)) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    std::vector<HeaderView> h = {
        {":path", "/stream"}, {":method", "POST"}, {"host", "example.com"}};
    auto result = handle_.startHttpStream(cluster_, h, "", false, 10000, *this);
    if (result.first != HttpCalloutInitResult::Success) {
      std::vector<HeaderView> rh = {{"x-error", "stream_init_failed"}};
      handle_.sendLocalResponse(500, rh, "", "");
      return HeadersStatus::StopAllAndBuffer;
    }
    stream_id_ = result.second;
    assertTrue(handle_.sendHttpStreamData(stream_id_, "chunk1", false), "c1");
    sent_chunks_++;
    assertTrue(handle_.sendHttpStreamData(stream_id_, "chunk2", false), "c2");
    sent_chunks_++;
    std::vector<HeaderView> tr = {{"x-request-trailer", "value"}};
    assertTrue(handle_.sendHttpStreamTrailers(stream_id_, tr), "tr");
    sent_trailers_ = true;
    return HeadersStatus::StopAllAndBuffer;
  }

  void onHttpStreamHeaders(uint64_t stream_id, absl::Span<const HeaderView> headers,
                           bool end_stream) override {
    assertEq(stream_id, stream_id_, "id");
    recv_headers_ = true;
  }
  void onHttpStreamData(uint64_t stream_id, absl::Span<const BufferView> body,
                        bool end_stream) override {
    assertEq(stream_id, stream_id_, "id");
    recv_chunks_++;
  }
  void onHttpStreamTrailers(uint64_t stream_id, absl::Span<const HeaderView> trailers) override {
    assertEq(stream_id, stream_id_, "id");
    recv_trailers_ = true;
  }

  void onHttpStreamComplete(uint64_t stream_id) override {
    assertEq(stream_id, stream_id_, "id");
    complete_ = true;
    std::vector<HeaderView> h = {
        {"x-stream-test", "bidirectional"},
        {"x-chunks-sent", std::to_string(sent_chunks_)},
        {"x-chunks-received", std::to_string(recv_chunks_)},
    };
    handle_.sendLocalResponse(200, h, "bidirectional_success", "");
  }

  void onHttpStreamReset(uint64_t stream_id, HttpStreamResetReason reason) override {}

  void onStreamComplete() override {
    assertTrue(sent_trailers_, "sentTrailers");
    assertTrue(recv_headers_, "recvHeaders");
    assertTrue(recv_chunks_ > 0, "recvChunks");
    assertTrue(recv_trailers_, "recvTrailers");
    assertTrue(complete_, "complete");
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }
  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }

private:
  HttpFilterHandle& handle_;
  std::string cluster_;
  uint64_t stream_id_{0};
  int sent_chunks_{0};
  bool sent_trailers_{false};
  bool recv_headers_{false};
  int recv_chunks_{0};
  bool recv_trailers_{false};
  bool complete_{false};
};

class HttpStreamBidiFilterFactory : public HttpFilterFactory {
public:
  HttpStreamBidiFilterFactory(std::string cluster) : cluster_(std::move(cluster)) {}
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HttpStreamBidiFilter>(handle, cluster_);
  }

private:
  std::string cluster_;
};

class HttpStreamBidiConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<HttpStreamBidiFilterFactory>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HttpStreamBidiConfigFactory, "http_stream_bidirectional");

// -----------------------------------------------------------------------------
// UpstreamReset
// -----------------------------------------------------------------------------

class UpstreamResetFilter : public HttpFilter, public HttpStreamCallback {
public:
  UpstreamResetFilter(HttpFilterHandle& handle, std::string cluster)
      : handle_(handle), cluster_(std::move(cluster)) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) override {
    std::vector<HeaderView> h = {{":path", "/reset"}, {":method", "GET"}, {"host", "example.com"}};
    auto result = handle_.startHttpStream(cluster_, h, "", true, 5000, *this);
    if (result.first != HttpCalloutInitResult::Success) {
      std::vector<HeaderView> rh = {{"x-error", "stream_init_failed"}};
      handle_.sendLocalResponse(500, rh, "", "");
      return HeadersStatus::StopAllAndBuffer;
    }
    stream_id_ = result.second;
    return HeadersStatus::StopAllAndBuffer;
  }

  void onHttpStreamReset(uint64_t stream_id, HttpStreamResetReason reason) override {
    assertEq(stream_id, stream_id_, "id");
    std::vector<HeaderView> rh = {{"x-reset", "true"}};
    handle_.sendLocalResponse(200, rh, "upstream_reset", "");
  }

  void onHttpStreamHeaders(uint64_t stream_id, absl::Span<const HeaderView> headers,
                           bool end_stream) override {}
  void onHttpStreamData(uint64_t stream_id, absl::Span<const BufferView> body,
                        bool end_stream) override {}
  void onHttpStreamTrailers(uint64_t stream_id, absl::Span<const HeaderView> trailers) override {}
  void onHttpStreamComplete(uint64_t stream_id) override {}

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) override {
    return HeadersStatus::Continue;
  }
  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    return BodyStatus::Continue;
  }
  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    return TrailersStatus::Continue;
  }
  void onStreamComplete() override {}

private:
  HttpFilterHandle& handle_;
  std::string cluster_;
  uint64_t stream_id_{0};
};

class UpstreamResetFilterFactory : public HttpFilterFactory {
public:
  UpstreamResetFilterFactory(std::string cluster) : cluster_(std::move(cluster)) {}
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<UpstreamResetFilter>(handle, cluster_);
  }

private:
  std::string cluster_;
};

class UpstreamResetConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            absl::string_view config_view) override {
    return std::make_unique<UpstreamResetFilterFactory>(std::string(config_view));
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(UpstreamResetConfigFactory, "upstream_reset");

} // namespace DynamicModules
} // namespace Envoy
