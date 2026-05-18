#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk.h"

namespace Envoy {
namespace DynamicModules {

// --- config_init_failure ---

class ConfigInitFailureConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    // Return null to simulate failure.
    return nullptr;
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(ConfigInitFailureConfigFactory, "config_init_failure");

// --- stats_callbacks ---

class StatsCallbacksFilter : public HttpFilter {
public:
  StatsCallbacksFilter(HttpFilterHandle& handle, MetricID streams_total,
                       MetricID concurrent_streams, MetricID magic_number, MetricID ones,
                       MetricID test_counter_vec, MetricID test_gauge_vec,
                       MetricID test_histogram_vec)
      : handle_(handle), streams_total_(streams_total), concurrent_streams_(concurrent_streams),
        magic_number_(magic_number), ones_(ones), test_counter_vec_(test_counter_vec),
        test_gauge_vec_(test_gauge_vec), test_histogram_vec_(test_histogram_vec) {
    handle_.incrementCounterValue(streams_total_, 1);
    handle_.incrementGaugeValue(concurrent_streams_, 1);
    handle_.setGaugeValue(magic_number_, 42);

    BufferView increment_tag("increment");
    handle_.incrementCounterValue(test_counter_vec_, 1, {{increment_tag}});

    BufferView increase_tag("increase");
    handle_.incrementGaugeValue(test_gauge_vec_, 1, {{increase_tag}});

    BufferView decrease_tag("decrease");
    handle_.incrementGaugeValue(test_gauge_vec_, 10, {{decrease_tag}});
    handle_.decrementGaugeValue(test_gauge_vec_, 8, {{decrease_tag}});

    BufferView set_tag("set");
    handle_.setGaugeValue(test_gauge_vec_, 9001, {{set_tag}});

    BufferView record_tag("record");
    handle_.recordHistogramValue(test_histogram_vec_, 1, {{record_tag}});
  }

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool) override {
    handle_.recordHistogramValue(ones_, 1);
    auto header = headers.getOne("header");
    BufferView header_tag(header);
    handle_.incrementCounterValue(test_counter_vec_, 1, {{header_tag}});
    handle_.incrementGaugeValue(test_gauge_vec_, 1, {{header_tag}});
    handle_.recordHistogramValue(test_histogram_vec_, 1, {{header_tag}});
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }

  void onStreamComplete() override {
    handle_.decrementGaugeValue(concurrent_streams_, 1);
    BufferView local_var("local_var");
    handle_.incrementCounterValue(test_counter_vec_, 1, {{local_var}});
    handle_.incrementGaugeValue(test_gauge_vec_, 1, {{local_var}});
    handle_.recordHistogramValue(test_histogram_vec_, 1, {{local_var}});
  }

  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
  MetricID streams_total_;
  MetricID concurrent_streams_;
  MetricID magic_number_;
  MetricID ones_;
  MetricID test_counter_vec_;
  MetricID test_gauge_vec_;
  MetricID test_histogram_vec_;
};

class StatsCallbacksFactory : public HttpFilterFactory {
public:
  StatsCallbacksFactory(HttpFilterConfigHandle& handle) {
    streams_total_ = handle.defineCounter("streams_total").first;
    concurrent_streams_ = handle.defineGauge("concurrent_streams").first;
    ones_ = handle.defineHistogram("ones").first;
    magic_number_ = handle.defineGauge("magic_number").first;
    BufferView test_label("test_label");
    test_counter_vec_ = handle.defineCounter("test_counter_vec", {{test_label}}).first;
    test_gauge_vec_ = handle.defineGauge("test_gauge_vec", {{test_label}}).first;
    test_histogram_vec_ = handle.defineHistogram("test_histogram_vec", {{test_label}}).first;
  }

  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<StatsCallbacksFilter>(handle, streams_total_, concurrent_streams_,
                                                  magic_number_, ones_, test_counter_vec_,
                                                  test_gauge_vec_, test_histogram_vec_);
  }

private:
  MetricID streams_total_;
  MetricID concurrent_streams_;
  MetricID magic_number_;
  MetricID ones_;
  MetricID test_counter_vec_;
  MetricID test_gauge_vec_;
  MetricID test_histogram_vec_;
};

class StatsCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                            std::string_view) override {
    return std::make_unique<StatsCallbacksFactory>(handle);
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(StatsCallbacksConfigFactory, "stats_callbacks");

// --- header_callbacks ---

class HeaderCallbacksFilter : public HttpFilter {
public:
  HeaderCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool) override {
    handle_.clearRouteCache();
    handle_.refreshRouteCluster();

    testHeaders(headers);

    // Attribute tests
    if (auto val = handle_.getAttributeNumber(AttributeID::SourcePort); !val || *val != 1234) {
      assert(false && "source port mismatch");
    }
    if (auto val = handle_.getAttributeString(AttributeID::SourceAddress); !val) {
      assert(false && "source address not found");
    }

    return HeadersStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap& trailers) override {
    testHeaders(trailers);
    return TrailersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap& headers, bool) override {
    testHeaders(headers);
    return HeadersStatus::Continue;
  }

  TrailersStatus onResponseTrailers(HeaderMap& trailers) override {
    testHeaders(trailers);
    return TrailersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  void testHeaders(HeaderMap& headers) {
    // Test single getter API
    if (auto val = headers.getOne("single"); val != "value") {
      assert(false && "header single mismatch");
    }
    if (auto val = headers.getOne("non-exist"); !val.empty()) {
      assert(false && "header non-exist found");
    }

    // Test multi getter API
    auto vals = headers.get("multi");
    if (vals.size() != 2 || vals[0] != "value1" || vals[1] != "value2") {
      assert(false && "header multi mismatch");
    }
    if (!headers.get("non-exist").empty()) {
      assert(false && "header non-exist found/not empty");
    }

    // Test setter API
    headers.set("new", "value");
    if (headers.getOne("new") != "value") {
      assert(false && "header new mismatch");
    }
    headers.remove("to-be-deleted");

    // Test adder API
    headers.add("multi", "value3");
    auto newVals = headers.get("multi");
    if (newVals.size() != 3 || newVals[0] != "value1" || newVals[1] != "value2" ||
        newVals[2] != "value3") {
      assert(false && "header multi values mismatch");
    }

    // Test all getter API
    auto all = headers.getAll();
    if (all.size() != 5) {
      assert(false && "header all length mismatch");
    }
    if (all[0].key() != "single" || all[0].value() != "value" || all[1].key() != "multi" ||
        all[1].value() != "value1" || all[2].key() != "multi" || all[2].value() != "value2" ||
        all[3].key() != "new" || all[3].value() != "value" || all[4].key() != "multi" ||
        all[4].value() != "value3") {
      assert(false && "header all mismatch");
    }
  }

  HttpFilterHandle& handle_;
};

class HeaderCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<HeaderCallbacksFilter>(handle);
  }
};

class HeaderCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<HeaderCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(HeaderCallbacksConfigFactory, "header_callbacks");

// --- send_response ---

class SendResponseFilter : public HttpFilter {
public:
  SendResponseFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    std::vector<HeaderView> headers;
    headers.push_back(HeaderView("header1", "value1"));
    headers.push_back(HeaderView("header2", "value2"));
    handle_.sendLocalResponse(200, headers, "Hello, World!", "");
    return HeadersStatus::Stop;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class SendResponseFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<SendResponseFilter>(handle);
  }
};

class SendResponseConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<SendResponseFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(SendResponseConfigFactory, "send_response");

// --- local_reply_callbacks ---

class LocalReplyCallbacksFilter : public HttpFilter {
public:
  explicit LocalReplyCallbacksFilter(HttpFilterHandle&) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

  LocalReplyStatus onLocalReply(uint32_t, std::string_view, bool) override {
    return LocalReplyStatus::ContinueAndResetStream;
  }
};

class LocalReplyCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<LocalReplyCallbacksFilter>(handle);
  }
};

class LocalReplyCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<LocalReplyCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(LocalReplyCallbacksConfigFactory, "local_reply_callbacks");

// --- reset_stream ---

class ResetStreamFilter : public HttpFilter {
public:
  explicit ResetStreamFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    [[maybe_unused]] const auto unused_reason =
        HttpFilterStreamResetReason::LocalRefusedStreamReset;
    handle_.resetStream(HttpFilterStreamResetReason::LocalReset, "details");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class ResetStreamFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<ResetStreamFilter>(handle);
  }
};

class ResetStreamConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<ResetStreamFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(ResetStreamConfigFactory, "reset_stream");

// --- send_go_away_and_close ---

class SendGoAwayAndCloseFilter : public HttpFilter {
public:
  explicit SendGoAwayAndCloseFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    handle_.sendGoAwayAndClose(true);
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class SendGoAwayAndCloseFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<SendGoAwayAndCloseFilter>(handle);
  }
};

class SendGoAwayAndCloseConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<SendGoAwayAndCloseFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(SendGoAwayAndCloseConfigFactory, "send_go_away_and_close");

// --- recreate_stream ---

class RecreateStreamFilter : public HttpFilter {
public:
  explicit RecreateStreamFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    std::vector<HeaderView> headers = {{":status", "302"}, {"location", "/recreated"}};
    if (!handle_.recreateStream(headers)) {
      std::abort();
    }
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class RecreateStreamFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<RecreateStreamFilter>(handle);
  }
};

class RecreateStreamConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<RecreateStreamFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(RecreateStreamConfigFactory, "recreate_stream");

// --- socket_option_callbacks ---

class SocketOptionCallbacksFilter : public HttpFilter {
public:
  explicit SocketOptionCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool) override {
    if (!handle_.setSocketOptionInt(1, 2, SocketOptionState::Prebind, SocketDirection::Upstream,
                                    123)) {
      std::abort();
    }
    if (!handle_.setSocketOptionBytes(3, 4, SocketOptionState::Bound, SocketDirection::Upstream,
                                      "socket-bytes")) {
      std::abort();
    }
    auto int_value =
        handle_.getSocketOptionInt(1, 2, SocketOptionState::Prebind, SocketDirection::Upstream);
    if (!int_value.has_value() || *int_value != 123) {
      std::abort();
    }
    auto bytes_value =
        handle_.getSocketOptionBytes(3, 4, SocketOptionState::Bound, SocketDirection::Upstream);
    if (!bytes_value.has_value() || *bytes_value != "socket-bytes") {
      std::abort();
    }
    headers.set("x-socket-option-callbacks", "true");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class SocketOptionCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<SocketOptionCallbacksFilter>(handle);
  }
};

class SocketOptionCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<SocketOptionCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(SocketOptionCallbacksConfigFactory, "socket_option_callbacks");

// --- span_callbacks ---

class SpanCallbacksFilter : public HttpFilter {
public:
  explicit SpanCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool) override {
    auto span = handle_.getActiveSpan();
    if (span != nullptr) {
      span->setTag("key", "value");
      span->setOperation("operation");
      span->log("event");
      span->setSampled(true);
      auto baggage = span->getBaggage("key");
      (void)baggage;
      span->setBaggage("key", "value");
      auto trace_id = span->getTraceID();
      (void)trace_id;
      auto span_id = span->getSpanID();
      (void)span_id;
      auto child = span->spawnChild("child");
      if (child != nullptr) {
        child->setTag("child-key", "child-value");
        child->finish();
      }
    }
    headers.set("x-span-callbacks", "true");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class SpanCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<SpanCallbacksFilter>(handle);
  }
};

class SpanCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<SpanCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(SpanCallbacksConfigFactory, "span_callbacks");

// --- cluster_callbacks ---

class ClusterCallbacksFilter : public HttpFilter {
public:
  explicit ClusterCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap& headers, bool) override {
    auto cluster_name = handle_.getClusterName();
    if (!cluster_name.has_value() || *cluster_name != "fake_cluster") {
      std::abort();
    }
    auto cluster_counts = handle_.getClusterHostCounts(0);
    if (!cluster_counts.has_value() || cluster_counts->total != 3 || cluster_counts->healthy != 2 ||
        cluster_counts->degraded != 1) {
      std::abort();
    }
    if (!handle_.setUpstreamOverrideHost("127.0.0.1:1", false)) {
      std::abort();
    }
    headers.set("x-cluster-callbacks", "true");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  BodyStatus onResponseBody(BodyBuffer&, bool) override { return BodyStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class ClusterCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<ClusterCallbacksFilter>(handle);
  }
};

class ClusterCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<ClusterCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(ClusterCallbacksConfigFactory, "cluster_callbacks");

// --- dynamic_metadata_callbacks ---

class DynamicMetadataCallbacksFilter : public HttpFilter {
public:
  DynamicMetadataCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    // No namespace.
    if (auto val = handle_.getMetadataNumber("no_namespace", "key"); val) {
      assert(false && "expected no metadata");
    }

    // Set a number.
    handle_.setMetadata("ns_req_header", "key", 123.0);
    if (auto val = handle_.getMetadataNumber("ns_req_header", "key"); !val || *val != 123.0) {
      assert(false && "metadata key mismatch");
    }

    // Try getting a number as string.
    if (auto val = handle_.getMetadataString("ns_req_header", "key"); val) {
      assert(false && "metadata type mismatch not detected");
    }

    // Try getting metadata from router, cluster, and host.
    // In C++ SDK namespaces like "envoy.filters.http.router" are needed if mapped directly,
    // but here we just rely on generic metadata get if possible or assume implementation detail
    // matching Go.
    // The Go code uses specialized enums (MetadataSourceTypeRoute etc) which map to namespaces.
    // The C++ SDK currently just takes namespace strings. We'll use values that match the backend
    // logic if we knew it. Assuming simple "metadata" namespace check for now based on typical
    // Envoy behavior but the Go code specifically asked for Route/Cluster/Host metadata. Since the
    // C++ SDK provided doesn't have `MetadataSourceType` enum exposed for `getMetadataString` but
    // takes a namespace string `ns`, we'll skip these specific route/cluster/host checks if the
    // namespace isn't obvious, or assume these keys are set under "envoy.lb" or similar for this
    // test context. However, looking at the Go code, it seems it expects specific values. Let's
    // assume the test setup puts them in "envoy.filters.http.dynamic_modules" or check if C++ SDK
    // has equivalents. The provided C++ SDK `getMetadataString` just takes `ns`.
    // We will follow the text literally:
    // Go: `GetMetadataString(shared.MetadataSourceTypeRoute, "metadata", "route_key")`
    // If the underlying C++ impl maps those enums to specific namespaces or ABI calls, we need to
    // match. The C++ SDK `getMetadataString` calls
    // `envoy_dynamic_module_callback_http_get_metadata_string` with
    // `envoy_dynamic_module_type_metadata_source_Dynamic`. It does *not* seem to expose
    // Route/Cluster/Host sources in the provided `sdk_internal.cc`.
    // The provided `sdk_internal.cc` hardcodes `envoy_dynamic_module_type_metadata_source_Dynamic`.
    // So we CANNOT implement the Route/Cluster/Host checks in C++ with the current SDK.
    // We will skip those checks for the C++ version.

    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override {
    // No namespace.
    if (auto val = handle_.getMetadataString("ns_req_body", "key"); val) {
      assert(false && "expected no metadata");
    }
    // Set a string.
    handle_.setMetadata("ns_req_body", "key", "value");
    if (auto val = handle_.getMetadataString("ns_req_body", "key"); !val || *val != "value") {
      assert(false && "metadata key mismatch");
    }
    // Try getting a string as number.
    if (auto val = handle_.getMetadataNumber("ns_req_body", "key"); val) {
      assert(false && "metadata type mismatch");
    }
    return BodyStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap&, bool) override {
    // No namespace.
    if (auto val = handle_.getMetadataString("ns_res_header", "key"); val) {
      assert(false && "expected no metadata");
    }
    // Set a number.
    handle_.setMetadata("ns_res_header", "key", 123.0);
    if (auto val = handle_.getMetadataNumber("ns_res_header", "key"); !val || *val != 123.0) {
      assert(false && "metadata key mismatch");
    }
    // Try getting a number as string.
    if (auto val = handle_.getMetadataString("ns_res_header", "key"); val) {
      assert(false && "metadata type mismatch");
    }
    return HeadersStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer&, bool) override {
    // No namespace.
    if (auto val = handle_.getMetadataString("ns_res_body", "key"); val) {
      assert(false && "expected no metadata");
    }
    // Set a string.
    handle_.setMetadata("ns_res_body", "key", "value");
    if (auto val = handle_.getMetadataString("ns_res_body", "key"); !val || *val != "value") {
      assert(false && "metadata key mismatch");
    }
    // Try getting a string as number.
    if (auto val = handle_.getMetadataNumber("ns_res_body", "key"); val) {
      assert(false && "metadata type mismatch");
    }

    // Test bool metadata.
    handle_.setMetadata("ns_res_body_bool", "bool_key", true);
    if (auto val = handle_.getMetadataBool("ns_res_body_bool", "bool_key"); !val || *val != true) {
      assert(false && "bool metadata mismatch");
    }
    // Set false.
    handle_.setMetadata("ns_res_body_bool", "bool_key", false);
    if (auto val = handle_.getMetadataBool("ns_res_body_bool", "bool_key"); !val || *val != false) {
      assert(false && "bool metadata mismatch for false");
    }
    // Try getting bool as string (should fail).
    if (auto val = handle_.getMetadataString("ns_res_body_bool", "bool_key"); val) {
      assert(false && "bool/string type mismatch not detected");
    }
    // Try getting bool as number (should fail).
    if (auto val = handle_.getMetadataNumber("ns_res_body_bool", "bool_key"); val) {
      assert(false && "bool/number type mismatch not detected");
    }

    // Test getMetadataKeys.
    handle_.setMetadata("ns_keys_test", "k1", "v1");
    handle_.setMetadata("ns_keys_test", "k2", 2.0);
    handle_.setMetadata("ns_keys_test", "k3", true);
    auto keys = handle_.getMetadataKeys("ns_keys_test");
    assert(keys.size() == 3 && "expected 3 keys");
    std::set<std::string> key_set(keys.begin(), keys.end());
    assert(key_set.count("k1") && key_set.count("k2") && key_set.count("k3") &&
           "missing expected keys");

    // Non-existing namespace returns empty.
    auto empty_keys = handle_.getMetadataKeys("non_existing_ns");
    assert(empty_keys.empty() && "expected empty keys for non-existing namespace");

    // Test getMetadataNamespaces.
    auto namespaces = handle_.getMetadataNamespaces();
    assert(!namespaces.empty() && "expected at least one namespace");
    std::set<std::string> ns_set(namespaces.begin(), namespaces.end());
    assert(ns_set.count("ns_keys_test") && "missing ns_keys_test in namespaces");
    assert(ns_set.count("ns_res_body_bool") && "missing ns_res_body_bool in namespaces");

    // Test list metadata.
    handle_.addMetadataList("ns_list", "list_key", 1.0);
    handle_.addMetadataList("ns_list", "list_key", 2.0);
    handle_.addMetadataList("ns_list", "list_key", 3.0);
    handle_.addMetadataList("ns_list", "str_list_key", "hello");
    handle_.addMetadataList("ns_list", "str_list_key", "world");
    handle_.addMetadataList("ns_list", "bool_list_key", true);
    handle_.addMetadataList("ns_list", "bool_list_key", false);

    return BodyStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class DynamicMetadataCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<DynamicMetadataCallbacksFilter>(handle);
  }
};

class DynamicMetadataCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<DynamicMetadataCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(DynamicMetadataCallbacksConfigFactory,
                                    "dynamic_metadata_callbacks");

// --- filter_state_callbacks ---

class FilterStateCallbacksFilter : public HttpFilter {
public:
  FilterStateCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override {
    [[maybe_unused]] const auto health_check = AttributeID::HealthCheck;
    if (handle_.getWorkerIndex() != 0) {
      std::abort();
    }
    auto typed_before = handle_.getFilterStateTyped("envoy.test.http_typed_object_for_rust");
    if (typed_before.has_value()) {
      std::abort();
    }
    if (!handle_.setFilterStateTyped("envoy.test.http_typed_object_for_rust", "typed_value")) {
      std::abort();
    }
    auto typed_after = handle_.getFilterStateTyped("envoy.test.http_typed_object_for_rust");
    if (!typed_after.has_value() || *typed_after != "typed_value") {
      std::abort();
    }
    testFilterState("req_header_key", "req_header_value");
    return HeadersStatus::Continue;
  }

  BodyStatus onRequestBody(BodyBuffer&, bool) override {
    testFilterState("req_body_key", "req_body_value");
    return BodyStatus::Continue;
  }

  TrailersStatus onRequestTrailers(HeaderMap&) override {
    testFilterState("req_trailer_key", "req_trailer_value");
    return TrailersStatus::Continue;
  }

  HeadersStatus onResponseHeaders(HeaderMap&, bool) override {
    testFilterState("res_header_key", "res_header_value");
    return HeadersStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer&, bool) override {
    testFilterState("res_body_key", "res_body_value");
    return BodyStatus::Continue;
  }

  TrailersStatus onResponseTrailers(HeaderMap&) override {
    testFilterState("res_trailer_key", "res_trailer_value");
    return TrailersStatus::Continue;
  }

  void onStreamComplete() override {
    testFilterState("stream_complete_key", "stream_complete_value");
  }

  void onDestroy() override {}

private:
  void testFilterState(std::string_view key, std::string_view value) {
    handle_.setFilterState(key, value);
    if (auto val = handle_.getFilterState(key); !val || *val != value) {
      assert(false && "filter state mismatch");
    }
    if (auto val = handle_.getFilterState("key"); val) {
      assert(false && "filter state key found");
    }
  }

  HttpFilterHandle& handle_;
};

class FilterStateCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<FilterStateCallbacksFilter>(handle);
  }
};

class FilterStateCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<FilterStateCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(FilterStateCallbacksConfigFactory, "filter_state_callbacks");

// --- body_callbacks ---

class BodyCallbacksFilter : public HttpFilter {
public:
  BodyCallbacksFilter(HttpFilterHandle& handle) : handle_(handle) {}

  BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) override {
    auto receivedBodyChunks = body.getChunks();
    handle_.log(LogLevel::Info, "Received body chunks");

    size_t receivedBodySize = body.getSize();
    body.drain(receivedBodySize);
    body.append("foo");
    if (end_stream) {
      body.append("end");
    }

    auto& bufferedBody = handle_.bufferedRequestBody();
    auto bufferedBodyChunks = bufferedBody.getChunks();
    handle_.log(LogLevel::Info, "Buffered body chunks");

    size_t bufferedBodySize = bufferedBody.getSize();
    bufferedBody.drain(bufferedBodySize);
    bufferedBody.append("foo");
    if (end_stream) {
      bufferedBody.append("end");
    }

    return BodyStatus::Continue;
  }

  BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) override {
    auto receivedBodyChunks = body.getChunks();
    handle_.log(LogLevel::Info, "Received body chunks");

    size_t receivedBodySize = body.getSize();
    body.drain(receivedBodySize);
    body.append("bar");
    if (end_stream) {
      body.append("end");
    }

    auto& bufferedBody = handle_.bufferedResponseBody();
    auto bufferedBodyChunks = bufferedBody.getChunks();
    handle_.log(LogLevel::Info, "Buffered body chunks");

    size_t bufferedBodySize = bufferedBody.getSize();
    bufferedBody.drain(bufferedBodySize);
    bufferedBody.append("bar");
    if (end_stream) {
      bufferedBody.append("end");
    }

    return BodyStatus::Continue;
  }

  HeadersStatus onRequestHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  TrailersStatus onRequestTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  HeadersStatus onResponseHeaders(HeaderMap&, bool) override { return HeadersStatus::Continue; }
  TrailersStatus onResponseTrailers(HeaderMap&) override { return TrailersStatus::Continue; }
  void onStreamComplete() override {}
  void onDestroy() override {}

private:
  HttpFilterHandle& handle_;
};

class BodyCallbacksFactory : public HttpFilterFactory {
public:
  std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) override {
    return std::make_unique<BodyCallbacksFilter>(handle);
  }
};

class BodyCallbacksConfigFactory : public HttpFilterConfigFactory {
public:
  std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle&, std::string_view) override {
    return std::make_unique<BodyCallbacksFactory>();
  }
};

REGISTER_HTTP_FILTER_CONFIG_FACTORY(BodyCallbacksConfigFactory, "body_callbacks");

} // namespace DynamicModules
} // namespace Envoy
