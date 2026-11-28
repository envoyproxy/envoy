#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/protobuf.h"

#include "test/config/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/filters/add_header_filter.pb.h"
#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/filters/test_filters.pb.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/registry.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "http_integration.h"

namespace Envoy {
namespace {

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";
constexpr absl::string_view expected_types[] = {
    "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
    "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
    "type.googleapis.com/envoy.admin.v3.EcdsConfigDump",
    "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
    "type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump",
    "type.googleapis.com/envoy.admin.v3.RoutesConfigDump",
    "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
using testing::Eq;
using testing::Ge;
using testing::Not;

class UpstreamHttpFilterIntegrationTestBase : public HttpIntegrationTest {
public:
  UpstreamHttpFilterIntegrationTestBase(Network::Address::IpVersion version,
                                        bool use_router_filters)
      : HttpIntegrationTest(Http::CodecType::HTTP2, version),
        use_router_filters_(use_router_filters) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    skip_tag_extraction_rule_check_ = true;
    autonomous_upstream_ = true;
  }

  void addStaticClusterFilter(const HttpFilterProto& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      *protocol_options.add_http_filters() = config;
      std::ignore = (*cluster->mutable_typed_extension_protocol_options())
                        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                            .PackFrom(protocol_options);
    });
  }

  void addStaticRouterFilter(const HttpFilterProto& config) {
    config_helper_.addConfigModifier(
        [config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          HttpFilterProto& router_filter = *hcm.mutable_http_filters()->rbegin();
          ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
          envoy::extensions::filters::http::router::v3::Router router;
          std::ignore = router_filter.typed_config().UnpackTo(&router);
          *router.add_upstream_http_filters() = config;
          std::ignore = router_filter.mutable_typed_config()->PackFrom(router);
        });
  }

  const HttpFilterProto getAddHeaderFilterConfig(const std::string& name, const std::string& key,
                                                 const std::string& value, bool disabled = false) {
    HttpFilterProto filter_config;
    filter_config.set_name(name);
    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(value);
    filter_config.set_disabled(disabled);
    std::ignore = filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  const HttpFilterProto getCodecFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("envoy.filters.http.upstream_codec");
    auto configuration = envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec();
    std::ignore = filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  void addStaticFilter(const std::string& name, const std::string& key, const std::string& value) {
    if (useRouterFilters()) {
      addStaticRouterFilter(getAddHeaderFilterConfig(name, key, value));
    } else {
      addStaticClusterFilter(getAddHeaderFilterConfig(name, key, value));
    }
  }

  void addCodecRouterFilter() { addStaticRouterFilter(getCodecFilterConfig()); }

  void addCodecClusterFilter() { addStaticClusterFilter(getCodecFilterConfig()); }

  void addCodecFilter() {
    if (useRouterFilters()) {
      addCodecRouterFilter();
    } else {
      addCodecClusterFilter();
    }
  }

  bool useRouterFilters() const { return use_router_filters_; }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string value) {
    expectHeaderKeyAndValue(headers, default_header_key_, value);
  }

  void expectHeaderKeyAndValue(std::unique_ptr<Http::RequestHeaderMap>& headers,
                               const std::string key, const std::string value) {
    auto header = headers->get(Http::LowerCaseString(key));
    ASSERT_FALSE(header.empty());
    EXPECT_EQ(value, header[0]->value().getStringView());
  }

  std::unique_ptr<Http::RequestHeaderMap> sendRequestAndGetHeaders() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    auto upstream_headers =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    EXPECT_TRUE(upstream_headers != nullptr);
    cleanupUpstreamAndDownstream();
    return upstream_headers;
  }

  void addStaticNoConfigFilter(const std::string& name) {
    HttpFilterProto filter_config;
    filter_config.set_name(name);
    std::ignore = filter_config.mutable_typed_config()->PackFrom(Envoy::Protobuf::Struct());
    if (useRouterFilters()) {
      addStaticRouterFilter(filter_config);
    } else {
      addStaticClusterFilter(filter_config);
    }
  }

  template <typename ProtoConfig>
  void addStaticTypedConfigFilter(const std::string& name, const ProtoConfig& config) {
    HttpFilterProto filter_config;
    filter_config.set_name(name);
    std::ignore = filter_config.mutable_typed_config()->PackFrom(config);
    if (useRouterFilters()) {
      addStaticRouterFilter(filter_config);
    } else {
      addStaticClusterFilter(filter_config);
    }
  }

  bool use_router_filters_{false};
  const std::string default_header_key_ = "header-key";
  const std::string default_header_value_ = "default-value";
};

class StaticRouterOrClusterFiltersIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  StaticRouterOrClusterFiltersIntegrationTest()
      : UpstreamHttpFilterIntegrationTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, BasicSuccess) {
  addStaticFilter("envoy.test.add_header_upstream", default_header_key_, default_header_value_);
  addCodecFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, default_header_value_);
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       ClusterUpstreamFiltersOverrideRouterUpstreamFilters) {
  // If both cluster and router upstream filters are set, only the cluster ones are applied.
  addStaticRouterFilter(
      getAddHeaderFilterConfig("envoy.test.add_header_upstream", "x-test-router", "aa"));
  addCodecRouterFilter();
  addStaticClusterFilter(
      getAddHeaderFilterConfig("envoy.test.add_header_upstream", "x-test-cluster", "bb"));
  addCodecClusterFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  EXPECT_THAT(*headers, Not(ContainsHeader("x-test-router", "aa")));
  EXPECT_THAT(*headers, ContainsHeader("x-test-cluster", "bb"));
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, ClusterUpstreamFiltersDisabled) {
  addStaticRouterFilter(
      getAddHeaderFilterConfig("envoy.test.add_header_upstream", "x-test-router", "aa", true));
  addCodecRouterFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  EXPECT_THAT(*headers, Not(ContainsHeader("x-test-router", "aa")));
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, RouterUpstreamFiltersDisabled) {
  addStaticClusterFilter(
      getAddHeaderFilterConfig("envoy.test.add_header_upstream", "x-test-cluster", "bb", true));
  addCodecClusterFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  EXPECT_THAT(*headers, Not(ContainsHeader("x-test-cluster", "bb")));
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       AsyncClientUsesClusterFiltersButNotRouterFilters) {
  // This filter intercepts the downstream request and reissues it via AsyncClient,
  // which should bypass the main filter chain and should use cluster upstream filters.
  config_helper_.prependFilter(R"(
  name: "envoy.test.async_upstream"
  typed_config:
      "@type": "type.googleapis.com/test.integration.filters.AsyncUpstreamFilterConfig"
  )");
  addStaticFilter("envoy.test.add_header_upstream", default_header_key_, default_header_value_);
  addCodecFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  if (useRouterFilters()) {
    EXPECT_THAT(*headers, Not(ContainsHeader(default_header_key_, default_header_value_)));
  } else {
    EXPECT_THAT(*headers, ContainsHeader(default_header_key_, default_header_value_));
  }
}

TEST_P(StaticRouterOrClusterFiltersIntegrationTest, TwoFilters) {
  addStaticFilter("foo", default_header_key_, "value1");
  addStaticFilter("bar", default_header_key_, "value2");
  addCodecFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "value1,value2");
}

// Verifies that an upstream filter calling sendLocalReply() from onHostSelected()
// (before decodeHeaders) correctly returns an error to the client without
// corrupting the connection state. Two requests on the same HTTP/2 connection
// (header-only + POST with body) prove the state machine handles early abort cleanly.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest, OnHostSelectedLocalReply) {
  addStaticTypedConfigFilter("local-reply-during-host-selection",
                             test::integration::filters::LocalReplyDuringHostSelectionConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1: header-only GET — rejected during onHostSelected.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("403", response1->headers().getStatusValue());

  // Request 2: POST with body — rejected during onHostSelected, body must be drained cleanly.
  Http::TestRequestHeaderMapImpl post_headers{
      {":method", "POST"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host"}};
  auto response2 = codec_client_->makeRequestWithBody(post_headers, "hello");
  EXPECT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("403", response2->headers().getStatusValue());

  // No upstream request was made — newStream() was never called.
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(upstream_headers == nullptr);

  cleanupUpstreamAndDownstream();
}

// Verify that a local reply sent from onHostSelected is not retried, even when the route has a
// retry policy that would normally retry on 4xx responses.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest, OnHostSelectedLocalReplyNoRetry) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("4xx");
        retry_policy->mutable_num_retries()->set_value(3);
      });

  addStaticTypedConfigFilter("local-reply-during-host-selection",
                             test::integration::filters::LocalReplyDuringHostSelectionConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());

  // No upstream connection was ever initiated — newStream() was never called on any attempt.
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(upstream_headers == nullptr);

  cleanupUpstreamAndDownstream();
}

namespace {

struct BidirectionalWatermarkCounts {
  absl::Mutex mutex;
  int upstream_high ABSL_GUARDED_BY(mutex){0};
  int upstream_low ABSL_GUARDED_BY(mutex){0};
  int downstream_high ABSL_GUARDED_BY(mutex){0};
  int downstream_low ABSL_GUARDED_BY(mutex){0};
  int encoder_continued ABSL_GUARDED_BY(mutex){0};

  enum class Mode {
    ObserveOnly,
    PrePoolEncoderHighThenTriggerLow,
    SelfUnsubscribeInDownstreamHighCallback,
    RaiseDecoderHighAndLocalReplyInDecodeHeaders,
    StopAllAndWatermarkOnEncodeHeaders,
    ResetStreamOnDemand,
  };
  Mode mode{Mode::ObserveOnly};
  std::string target_path;

  Event::Dispatcher* active_dispatcher ABSL_GUARDED_BY(mutex){nullptr};
  std::weak_ptr<std::atomic<bool>> active_alive ABSL_GUARDED_BY(mutex);
  std::function<void()> action_fn ABSL_GUARDED_BY(mutex);
  bool action_completed ABSL_GUARDED_BY(mutex){false};

  bool waitForCount(int BidirectionalWatermarkCounts::* field, int min_count) {
    absl::MutexLock lock(mutex);
    auto cond = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) { return this->*field >= min_count; };
    return mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10));
  }

  void runActiveAction() {
    Event::Dispatcher* disp = nullptr;
    std::weak_ptr<std::atomic<bool>> alive;
    std::function<void()> fn;
    {
      absl::MutexLock lock(mutex);
      disp = active_dispatcher;
      alive = active_alive;
      fn = action_fn;
      action_completed = false;
    }
    ASSERT_NE(disp, nullptr);
    disp->post([this, alive = std::move(alive), fn = std::move(fn)]() {
      if (auto guard = alive.lock(); guard != nullptr && guard->load()) {
        fn();
      }
      absl::MutexLock lock(mutex);
      action_completed = true;
    });
    absl::MutexLock lock(mutex);
    auto cond = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) { return action_completed; };
    ASSERT_TRUE(mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10)));
  }
};

// Upstream HTTP filter that subscribes to both upstream (request-direction) and downstream
// (response-direction) watermark callbacks and optionally triggers edge-case watermark behaviors.
class BidirectionalWatermarkObserverFilter : public Http::StreamFilter {
public:
  explicit BidirectionalWatermarkObserverFilter(std::shared_ptr<BidirectionalWatermarkCounts> state)
      : state_(std::move(state)), alive_(std::make_shared<std::atomic<bool>>(true)),
        upstream_cb_(*this), downstream_cb_(*this) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    if (!state_->target_path.empty() && headers.getPathValue() != state_->target_path) {
      is_target_stream_ = false;
      return Http::FilterHeadersStatus::Continue;
    }
    is_target_stream_ = true;
    if (state_->mode == BidirectionalWatermarkCounts::Mode::PrePoolEncoderHighThenTriggerLow) {
      encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
      absl::MutexLock lock(state_->mutex);
      state_->active_dispatcher = &callbacks_->dispatcher();
      state_->active_alive = alive_;
      state_->action_fn = [this]() {
        encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
      };
    } else if (state_->mode ==
               BidirectionalWatermarkCounts::Mode::RaiseDecoderHighAndLocalReplyInDecodeHeaders) {
      callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
      callbacks_->sendLocalReply(Http::Code::Forbidden, "blocked", nullptr, std::nullopt,
                                 "watermark_test_local_reply");
      return Http::FilterHeadersStatus::StopIteration;
    } else if (state_->mode == BidirectionalWatermarkCounts::Mode::ResetStreamOnDemand) {
      absl::MutexLock lock(state_->mutex);
      state_->active_dispatcher = &callbacks_->dispatcher();
      state_->active_alive = alive_;
      state_->action_fn = [this]() { callbacks_->resetStream(); };
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->addUpstreamWatermarkCallbacks(upstream_cb_);
    upstream_subscribed_ = true;
    callbacks_->addDownstreamWatermarkCallbacks(downstream_cb_);
    downstream_subscribed_ = true;
  }

  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    if (is_target_stream_ &&
        state_->mode == BidirectionalWatermarkCounts::Mode::StopAllAndWatermarkOnEncodeHeaders &&
        !stopped_encode_headers_) {
      stopped_encode_headers_ = true;
      continue_cb_ = encoder_callbacks_->dispatcher().createSchedulableCallback([this]() {
        if (!alive_->load()) {
          return;
        }
        {
          absl::MutexLock lock(state_->mutex);
          ++state_->encoder_continued;
        }
        encoder_callbacks_->continueEncoding();
      });
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  void onDestroy() override {
    alive_->store(false);
    if (continue_cb_ != nullptr) {
      continue_cb_->cancel();
    }
    if (callbacks_ != nullptr) {
      if (upstream_subscribed_) {
        callbacks_->removeUpstreamWatermarkCallbacks(upstream_cb_);
        upstream_subscribed_ = false;
      }
      if (downstream_subscribed_) {
        callbacks_->removeDownstreamWatermarkCallbacks(downstream_cb_);
        downstream_subscribed_ = false;
      }
    }
  }

private:
  class UpstreamSubscriber : public Http::UpstreamWatermarkCallbacks {
  public:
    explicit UpstreamSubscriber(BidirectionalWatermarkObserverFilter& parent) : parent_(parent) {}
    void onAboveWriteBufferHighWatermark() override {
      absl::MutexLock lock(parent_.state_->mutex);
      ++parent_.state_->upstream_high;
    }
    void onBelowWriteBufferLowWatermark() override {
      absl::MutexLock lock(parent_.state_->mutex);
      ++parent_.state_->upstream_low;
    }
    BidirectionalWatermarkObserverFilter& parent_;
  };

  class DownstreamSubscriber : public Http::DownstreamWatermarkCallbacks {
  public:
    explicit DownstreamSubscriber(BidirectionalWatermarkObserverFilter& parent) : parent_(parent) {}
    void onAboveWriteBufferHighWatermark() override {
      {
        absl::MutexLock lock(parent_.state_->mutex);
        ++parent_.state_->downstream_high;
      }
      if (parent_.state_->mode ==
              BidirectionalWatermarkCounts::Mode::SelfUnsubscribeInDownstreamHighCallback &&
          parent_.is_target_stream_ && parent_.downstream_subscribed_) {
        parent_.downstream_subscribed_ = false;
        parent_.callbacks_->removeDownstreamWatermarkCallbacks(*this);
        return;
      }
      if (parent_.state_->mode ==
              BidirectionalWatermarkCounts::Mode::StopAllAndWatermarkOnEncodeHeaders &&
          parent_.stopped_encode_headers_ && !parent_.scheduled_continue_ &&
          parent_.continue_cb_ != nullptr) {
        parent_.scheduled_continue_ = true;
        parent_.continue_cb_->scheduleCallbackNextIteration();
      }
    }
    void onBelowWriteBufferLowWatermark() override {
      absl::MutexLock lock(parent_.state_->mutex);
      ++parent_.state_->downstream_low;
    }
    BidirectionalWatermarkObserverFilter& parent_;
  };

  std::shared_ptr<BidirectionalWatermarkCounts> state_;
  std::shared_ptr<std::atomic<bool>> alive_;
  Http::StreamDecoderFilterCallbacks* callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  Event::SchedulableCallbackPtr continue_cb_;
  UpstreamSubscriber upstream_cb_;
  DownstreamSubscriber downstream_cb_;
  bool is_target_stream_{true};
  bool upstream_subscribed_{false};
  bool downstream_subscribed_{false};
  bool stopped_encode_headers_{false};
  bool scheduled_continue_{false};
};

class BidirectionalWatermarkObserverFilterFactory
    : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  explicit BidirectionalWatermarkObserverFilterFactory(
      std::shared_ptr<BidirectionalWatermarkCounts> state)
      : state_(std::move(state)) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::UpstreamFactoryContext&) override {
    auto state = state_;
    return [state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<BidirectionalWatermarkObserverFilter>(state));
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        test::integration::filters::BidirectionalWatermarkObserverFilterConfig>();
  }
  std::string name() const override { return "envoy.test.bidirectional_watermark_observer"; }

private:
  std::shared_ptr<BidirectionalWatermarkCounts> state_;
};

// Downstream HCM filter that raises DownstreamFilterManager's high watermark in decodeHeaders()
// (before Router::Filter receives response headers) and holds it until released by the test thread.
struct DownstreamWatermarkDriverState {
  absl::Mutex mutex;
  Event::Dispatcher* dispatcher ABSL_GUARDED_BY(mutex){nullptr};
  std::weak_ptr<std::atomic<bool>> alive ABSL_GUARDED_BY(mutex);
  std::function<void()> lower_fn ABSL_GUARDED_BY(mutex);
  bool lowered ABSL_GUARDED_BY(mutex){false};

  void releaseBackpressure() {
    Event::Dispatcher* disp = nullptr;
    std::weak_ptr<std::atomic<bool>> alive_ref;
    std::function<void()> fn;
    {
      absl::MutexLock lock(mutex);
      disp = dispatcher;
      alive_ref = alive;
      fn = lower_fn;
      lowered = false;
    }
    ASSERT_NE(disp, nullptr);
    disp->post([this, alive_ref = std::move(alive_ref), fn = std::move(fn)]() {
      if (auto guard = alive_ref.lock(); guard != nullptr && guard->load()) {
        fn();
      }
      absl::MutexLock lock(mutex);
      lowered = true;
    });
    absl::MutexLock lock(mutex);
    auto cond = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) { return lowered; };
    ASSERT_TRUE(mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10)));
  }
};

class DownstreamPreResponseWatermarkFilter : public Http::StreamFilter {
public:
  explicit DownstreamPreResponseWatermarkFilter(
      std::shared_ptr<DownstreamWatermarkDriverState> state)
      : state_(std::move(state)), alive_(std::make_shared<std::atomic<bool>>(true)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
    raised_ = true;
    absl::MutexLock lock(state_->mutex);
    state_->dispatcher = &decoder_callbacks_->dispatcher();
    state_->alive = alive_;
    state_->lower_fn = [this]() {
      if (raised_) {
        raised_ = false;
        encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
      }
    };
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  void onDestroy() override { alive_->store(false); }

private:
  std::shared_ptr<DownstreamWatermarkDriverState> state_;
  std::shared_ptr<std::atomic<bool>> alive_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  bool raised_{false};
};

class DownstreamPreResponseWatermarkFilterFactory
    : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  explicit DownstreamPreResponseWatermarkFilterFactory(
      std::shared_ptr<DownstreamWatermarkDriverState> state)
      : state_(std::move(state)) {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    auto state = state_;
    return [state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<DownstreamPreResponseWatermarkFilter>(state));
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        test::integration::filters::BidirectionalWatermarkObserverFilterConfig>();
  }
  std::string name() const override { return "envoy.test.downstream_preresponse_watermark"; }

private:
  std::shared_ptr<DownstreamWatermarkDriverState> state_;
};

} // namespace

// Verifies that an upstream HTTP filter receives real backpressure in both directions:
// - slow upstream backend triggers UpstreamWatermarkCallbacks high/low;
// - slow downstream client triggers DownstreamWatermarkCallbacks high/low without double
//   counting upstream_flow_control_paused_reading_total.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest, BidirectionalWatermarkPropagation) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);

  config_helper_.setBufferLimits(1024, 1024);
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl post_headers{
      {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto encoder_decoder = codec_client_->startRequest(post_headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // 1. Request direction: pause upstream reads and push > buffer limit from client.
  upstream_request_->readDisable(true);
  const std::string large_request(1024 * 1024, 'a');
  codec_client_->sendData(*request_encoder_, large_request, true);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::upstream_high, 1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_backed_up_total", Ge(1));

  // Resume upstream reads and verify request drains and completes.
  upstream_request_->readDisable(false);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_EQ(large_request, upstream_request_->body().toString());
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::upstream_low, 1));

  // 2. Response direction: pause downstream client reads and push > buffer limit from backend.
  request_encoder_->getStream().readDisable(true);
  const std::string large_response(1024 * 1024, 'b');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(large_response, false);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_paused_reading_total",
                               Ge(1));
  {
    absl::MutexLock lock(counts->mutex);
    // Downstream watermark must not double-increment paused_reading_total vs subscriber delivery.
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
                  ->value(),
              static_cast<uint64_t>(counts->downstream_high));
  }

  // Resume downstream client reads, send terminal frame, and verify response completes.
  request_encoder_->getStream().readDisable(false);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_low, 1));
  upstream_request_->encodeData("end", true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(large_response + "end", response->body());

  cleanupUpstreamAndDownstream();
}

// Verifies simultaneous full-duplex backpressure (slow backend + slow client on the same stream).
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       BidirectionalWatermarkFullDuplexSlowClientAndBackend) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);

  config_helper_.setBufferLimits(1024, 1024);
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl post_headers{
      {":method", "POST"}, {":path", "/duplex"}, {":scheme", "http"}, {":authority", "host"}};
  auto encoder_decoder = codec_client_->startRequest(post_headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  upstream_request_->readDisable(true);
  request_encoder_->getStream().readDisable(true);
  const std::string req_body(512 * 1024, 'r');
  const std::string resp_body(512 * 1024, 's');
  codec_client_->sendData(*request_encoder_, req_body, false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(resp_body, false);

  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::upstream_high, 1));
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));

  upstream_request_->readDisable(false);
  request_encoder_->getStream().readDisable(false);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::upstream_low, 1));
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_low, 1));

  codec_client_->sendData(*request_encoder_, "req-end", true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_EQ(req_body + "req-end", upstream_request_->body().toString());

  upstream_request_->encodeData("resp-end", true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(resp_body + "resp-end", response->body());

  {
    absl::MutexLock lock(counts->mutex);
    EXPECT_EQ(counts->upstream_high, counts->upstream_low);
    EXPECT_EQ(counts->downstream_high, counts->downstream_low);
  }
  cleanupUpstreamAndDownstream();
}

// Verifies that a deferred downstream watermark flushes once on 100 Continue.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest, DeferredDownstreamWatermarkWith100Continue) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);
  auto ds_state = std::make_shared<DownstreamWatermarkDriverState>();
  DownstreamPreResponseWatermarkFilterFactory ds_factory(ds_state);
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> ds_registration(
      ds_factory);

  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_proxy_100_continue(true); });
  config_helper_.prependFilter(R"EOF(
name: envoy.test.downstream_preresponse_watermark
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BidirectionalWatermarkObserverFilterConfig
)EOF");
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/100"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"expect", "100-continue"}};
  auto enc_dec = codec_client_->startRequest(headers);
  auto response = std::move(enc_dec.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  {
    absl::MutexLock lock(counts->mutex);
    EXPECT_EQ(counts->downstream_high, 0);
  }

  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_paused_reading_total",
                               Ge(1));

  ds_state->releaseBackpressure();
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_low, 1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                               Ge(1));

  codec_client_->sendData(enc_dec.first, "payload", true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData("ok", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("ok", response->body());

  cleanupUpstreamAndDownstream();
}

// Verifies that a retriable 503 response does not flush deferred downstream watermarks or charge
// upstream_flow_control_paused_reading_total on the retried attempt.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       DeferredDownstreamWatermarkNotFlushedOnRetriable503) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);
  auto ds_state = std::make_shared<DownstreamWatermarkDriverState>();
  DownstreamPreResponseWatermarkFilterFactory ds_factory(ds_state);
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> ds_registration(
      ds_factory);

  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(1);
      });
  config_helper_.prependFilter(R"EOF(
name: envoy.test.downstream_preresponse_watermark
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BidirectionalWatermarkObserverFilterConfig
)EOF");
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  FakeStreamPtr attempt1;
  FakeStreamPtr attempt2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, attempt1));
  ASSERT_TRUE(attempt1->waitForEndStream(*dispatcher_));

  const uint64_t paused_before_503 =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  attempt1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, attempt2));
  ASSERT_TRUE(attempt2->waitForEndStream(*dispatcher_));

  const uint64_t paused_after_503 =
      test_server_->counter("cluster.cluster_0.upstream_flow_control_paused_reading_total")
          ->value();
  int downstream_high_after_503 = 0;
  {
    absl::MutexLock lock(counts->mutex);
    downstream_high_after_503 = counts->downstream_high;
  }

  // Attempt #2 succeeds and commits the response (flushing deferred watermark once).
  attempt2->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));
  ds_state->releaseBackpressure();
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_low, 1));
  attempt2->encodeData("retry-ok", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("retry-ok", response->body());

  EXPECT_EQ(paused_after_503, paused_before_503);
  EXPECT_EQ(downstream_high_after_503, 0);

  cleanupUpstreamAndDownstream();
}

// Verifies that deferred downstream watermarks compose cleanly with an upstream encoder filter
// returning StopAllIterationAndWatermark and buffering over the buffer limit.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       DeferredDownstreamWatermarkWithUpstreamEncoderStopAllAndWatermark) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  counts->mode = BidirectionalWatermarkCounts::Mode::StopAllAndWatermarkOnEncodeHeaders;
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);
  auto ds_state = std::make_shared<DownstreamWatermarkDriverState>();
  DownstreamPreResponseWatermarkFilterFactory ds_factory(ds_state);
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> ds_registration(
      ds_factory);

  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.prependFilter(R"EOF(
name: envoy.test.downstream_preresponse_watermark
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BidirectionalWatermarkObserverFilterConfig
)EOF");
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  const std::string body(64 * 1024, 'm');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(body, false);

  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::encoder_continued, 1));
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 2));

  ds_state->releaseBackpressure();
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_low, 2));

  upstream_request_->encodeData("tail", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body + "tail", response->body());

  cleanupUpstreamAndDownstream();
}

// Verifies that lowering a pre-pool encoder high watermark does not steal an active downstream
// readDisable(true) and prematurely resume upstream reading while downstream is paused.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       PrePoolEncoderHighWatermarkNotStolenByDownstreamBackpressure) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  counts->mode = BidirectionalWatermarkCounts::Mode::PrePoolEncoderHighThenTriggerLow;
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);
  auto ds_state = std::make_shared<DownstreamWatermarkDriverState>();
  DownstreamPreResponseWatermarkFilterFactory ds_factory(ds_state);
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> ds_registration(
      ds_factory);

  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.prependFilter(R"EOF(
name: envoy.test.downstream_preresponse_watermark
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BidirectionalWatermarkObserverFilterConfig
)EOF");
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Committing response headers flushes the deterministic downstream high watermark (paused == 1).
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_paused_reading_total",
                               Eq(1));
  EXPECT_EQ(0U,
            test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
                ->value());

  // Lowering the pre-pool encoder watermark must NOT steal downstream's readDisable(true).
  counts->runActiveAction();
  EXPECT_EQ(0U,
            test_server_->counter("cluster.cluster_0.upstream_flow_control_resumed_reading_total")
                ->value());

  // Releasing downstream backpressure resumes upstream reading once and completes the response.
  ds_state->releaseBackpressure();
  test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                               Eq(1));
  upstream_request_->encodeData("end", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("end", response->body());
  cleanupUpstreamAndDownstream();
}

// Verifies that an upstream filter unsubscribing itself inside DownstreamWatermarkCallbacks does
// not invalidate FilterManager iteration or cause heap-use-after-free.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       SelfUnsubscribeInDownstreamWatermarkCallbackDoesNotUaf) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  counts->mode = BidirectionalWatermarkCounts::Mode::SelfUnsubscribeInDownstreamHighCallback;
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);

  config_helper_.setBufferLimits(1024, 1024);
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "", true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  request_encoder_->getStream().readDisable(true);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  const std::string body(512 * 1024, 'v');
  upstream_request_->encodeData(body, true);

  ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));
  request_encoder_->getStream().readDisable(false);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(body, response->body());

  cleanupUpstreamAndDownstream();
}

// Verifies that local reply, client reset, backend reset, and filter reset while above high
// watermark all unwind cleanly without leaking watermarks on the shared HTTP/2 connection.
TEST_P(StaticRouterOrClusterFiltersIntegrationTest,
       ResetAndLocalReplyWhileAboveHighWatermarkReuseH2Connection) {
  autonomous_upstream_ = false;
  auto counts = std::make_shared<BidirectionalWatermarkCounts>();
  counts->target_path = "/local-reply";
  counts->mode = BidirectionalWatermarkCounts::Mode::RaiseDecoderHighAndLocalReplyInDecodeHeaders;
  BidirectionalWatermarkObserverFilterFactory factory(counts);
  Registry::InjectFactory<Server::Configuration::UpstreamHttpFilterConfigFactory> registration(
      factory);

  config_helper_.setBufferLimits(1024, 1024);
  addStaticTypedConfigFilter(
      "envoy.test.bidirectional_watermark_observer",
      test::integration::filters::BidirectionalWatermarkObserverFilterConfig());
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // 1. Pre-pool filter raises decoder high watermark and sends 403 local reply.
  {
    Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                           {":path", "/local-reply"},
                                           {":scheme", "http"},
                                           {":authority", "host"}};
    auto resp = codec_client_->makeRequestWithBody(headers, std::string(16 * 1024, 'a'));
    ASSERT_TRUE(resp->waitForEndStream());
    EXPECT_EQ("403", resp->headers().getStatusValue());
  }

  // 2. Client resets stream while request direction is above high watermark.
  {
    Http::TestRequestHeaderMapImpl post_headers{
        {":method", "POST"}, {":path", "/client-rst"}, {":scheme", "http"}, {":authority", "host"}};
    auto enc_dec = codec_client_->startRequest(post_headers);
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    FakeStreamPtr up;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, up));
    ASSERT_TRUE(up->waitForHeadersComplete());

    up->readDisable(true);
    codec_client_->sendData(enc_dec.first, std::string(512 * 1024, 'b'), false);
    test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_backed_up_total", Ge(1));

    codec_client_->sendReset(enc_dec.first);
    ASSERT_TRUE(up->waitForReset());
  }

  // 3. Backend resets stream while response direction is above high watermark.
  {
    Http::TestRequestHeaderMapImpl get_headers{
        {":method", "GET"}, {":path", "/backend-rst"}, {":scheme", "http"}, {":authority", "host"}};
    auto enc_dec = codec_client_->startRequest(get_headers);
    codec_client_->sendData(enc_dec.first, "", true);
    FakeStreamPtr up;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, up));
    ASSERT_TRUE(up->waitForEndStream(*dispatcher_));

    enc_dec.first.getStream().readDisable(true);
    up->encodeHeaders(default_response_headers_, false);
    up->encodeData(std::string(512 * 1024, 'c'), false);
    test_server_->waitForCounter("cluster.cluster_0.upstream_flow_control_paused_reading_total",
                                 Ge(1));

    up->encodeResetStream();
    ASSERT_TRUE(enc_dec.second->waitForReset());
  }

  // 4. Upstream filter resets stream while both directions are above high watermark.
  {
    {
      absl::MutexLock lock(counts->mutex);
      counts->mode = BidirectionalWatermarkCounts::Mode::ResetStreamOnDemand;
      counts->target_path = "/filter-rst";
      counts->upstream_high = 0;
      counts->downstream_high = 0;
    }
    Http::TestRequestHeaderMapImpl post_headers{
        {":method", "POST"}, {":path", "/filter-rst"}, {":scheme", "http"}, {":authority", "host"}};
    auto enc_dec = codec_client_->startRequest(post_headers);
    FakeStreamPtr up;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, up));
    ASSERT_TRUE(up->waitForHeadersComplete());

    up->readDisable(true);
    enc_dec.first.getStream().readDisable(true);
    codec_client_->sendData(enc_dec.first, std::string(512 * 1024, 'd'), false);
    up->encodeHeaders(default_response_headers_, false);
    up->encodeData(std::string(512 * 1024, 'e'), false);
    ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::upstream_high, 1));
    ASSERT_TRUE(counts->waitForCount(&BidirectionalWatermarkCounts::downstream_high, 1));

    counts->runActiveAction();
    ASSERT_TRUE(enc_dec.second->waitForReset());
    ASSERT_TRUE(up->waitForReset());
  }

  // 5. Follow-up request on the same HTTP/2 connections succeeds cleanly.
  {
    {
      absl::MutexLock lock(counts->mutex);
      counts->target_path = "/none";
    }
    Http::TestRequestHeaderMapImpl ok_headers{
        {":method", "GET"}, {":path", "/ok"}, {":scheme", "http"}, {":authority", "host"}};
    auto ok_resp = codec_client_->makeHeaderOnlyRequest(ok_headers);
    FakeStreamPtr up;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, up));
    ASSERT_TRUE(up->waitForEndStream(*dispatcher_));
    up->encodeHeaders(default_response_headers_, false);
    up->encodeData("ok", true);
    ASSERT_TRUE(ok_resp->waitForEndStream());
    EXPECT_EQ("200", ok_resp->headers().getStatusValue());
    EXPECT_EQ("ok", ok_resp->body());
  }

  cleanupUpstreamAndDownstream();
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, StaticRouterOrClusterFiltersIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()));

class StaticRouterAndClusterFiltersIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  StaticRouterAndClusterFiltersIntegrationTest()
      : UpstreamHttpFilterIntegrationTestBase(GetParam(), false) {}

  void routeRetryAndFilterReturnStopIteration(bool send_body) {
    autonomous_upstream_ = false;
    config_helper_.prependFilter(R"EOF(
name: encode-headers-return-stop-iteration-filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.EncodeHeadersReturnStopIterationFilterConfig
)EOF",
                                 false);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* route = hcm.mutable_route_config()
                            ->mutable_virtual_hosts(0)
                            ->mutable_routes(0)
                            ->mutable_route();
          route->mutable_timeout()->set_seconds(60);
          auto* retry_policy = route->mutable_retry_policy();
          retry_policy->set_retry_on("5xx,connect-failure,refused-stream");
          retry_policy->mutable_num_retries()->set_value(5);
          retry_policy->mutable_per_try_timeout()->set_seconds(30);
          retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_seconds(1);
        });
    initialize();

    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
    if (send_body) {
      upstream_request_->encodeData(100, true);
    } else {
      upstream_request_->encodeTrailers(
          Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
    }
    EXPECT_TRUE(upstream_request_->complete());

    // Router filter send retries.
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
    EXPECT_TRUE(upstream_request_->complete());
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }
};

// Only cluster-specified filters should be applied.
TEST_P(StaticRouterAndClusterFiltersIntegrationTest, StaticRouterAndClusterFilters) {
  addStaticClusterFilter(
      getAddHeaderFilterConfig("foo", default_header_key_, "value-from-cluster"));
  addStaticRouterFilter(getAddHeaderFilterConfig("bar", default_header_key_, "value-from-router"));
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "value-from-cluster");
}

// Test route retries on 5xx and an upstream filter returns StopIteration.
TEST_P(StaticRouterAndClusterFiltersIntegrationTest, RouterRetrySendBody) {
  routeRetryAndFilterReturnStopIteration(/*send_body*/ true);
}

TEST_P(StaticRouterAndClusterFiltersIntegrationTest, RouterRetrySendTrailers) {
  routeRetryAndFilterReturnStopIteration(/*send_body*/ false);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, StaticRouterAndClusterFiltersIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

class GrpcClientIntegrationParamTestWithVariantUpstreamConfig
    : public testing::TestWithParam<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>>& p) {
    return fmt::format(
        "{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(std::get<0>(p.param))),
        std::get<1>(std::get<0>(p.param)) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                          : "EnvoyGrpc",
        std::get<1>(p.param) ? "WithRouterFilters" : "WithClusterFilters");
  }
};

class UpstreamHttpExtensionDiscoveryIntegrationTestBase
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public UpstreamHttpFilterIntegrationTestBase {
public:
  UpstreamHttpExtensionDiscoveryIntegrationTestBase(Network::Address::IpVersion version,
                                                    bool use_router_filters)
      : UpstreamHttpFilterIntegrationTestBase(version, use_router_filters) {}

  void setDynamicFilterConfig(HttpFilterProto* filter, const std::string& name,
                              bool apply_without_warming, bool set_default_config = true,
                              bool rate_limit = false, bool second_connection = false) {
    filter->set_name(name);

    auto* discovery = filter->mutable_config_discovery();
    discovery->add_type_urls("type.googleapis.com/test.integration.filters.AddHeaderFilterConfig");
    if (set_default_config) {
      auto default_configuration = test::integration::filters::AddHeaderFilterConfig();
      default_configuration.set_header_key(default_header_key_);
      default_configuration.set_header_value(default_header_value_);
      std::ignore = discovery->mutable_default_config()->PackFrom(default_configuration);
    }

    discovery->set_apply_default_config_without_warming(apply_without_warming);
    discovery->mutable_config_source()->set_resource_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    if (rate_limit) {
      api_config_source->mutable_rate_limit_settings()->mutable_max_tokens()->set_value(10);
    }
    auto* grpc_service = api_config_source->add_grpc_services();
    if (!second_connection) {
      setGrpcService(*grpc_service, std::string(EcdsClusterName),
                     getEcdsFakeUpstream().localAddress());
    } else {
      setGrpcService(*grpc_service, std::string(Ecds2ClusterName),
                     getEcds2FakeUpstream().localAddress());
    }
  }

  void addDynamicClusterFilter(const std::string& name, bool apply_without_warming,
                               bool set_default_config = true, bool rate_limit = false,
                               bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      auto* filter = protocol_options.add_http_filters();
      setDynamicFilterConfig(filter, name, apply_without_warming, set_default_config, rate_limit,
                             second_connection);
      std::ignore = (*cluster->mutable_typed_extension_protocol_options())
                        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                            .PackFrom(protocol_options);
    });
  }

  void addDynamicRouterFilter(const std::string& name, bool apply_without_warming,
                              bool set_default_config = true, bool rate_limit = false,
                              bool second_connection = false) {
    config_helper_.addConfigModifier(
        [name, apply_without_warming, set_default_config, rate_limit, second_connection, this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          HttpFilterProto& router_filter = *hcm.mutable_http_filters(0);
          ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
          envoy::extensions::filters::http::router::v3::Router router;
          std::ignore = router_filter.typed_config().UnpackTo(&router);
          auto* filter = router.add_upstream_http_filters();
          setDynamicFilterConfig(filter, name, apply_without_warming, set_default_config,
                                 rate_limit, second_connection);
          std::ignore = router_filter.mutable_typed_config()->PackFrom(router);
        });
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    if (useRouterFilters()) {
      addDynamicRouterFilter(name, apply_without_warming, set_default_config, rate_limit,
                             second_connection);
    } else {
      addDynamicClusterFilter(name, apply_without_warming, set_default_config, rate_limit,
                              second_connection);
    }
  }

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ecds_cluster->mutable_typed_extension_protocol_options()->clear();
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);
    addEcdsCluster(std::string(EcdsClusterName));
    // Use gRPC LDS instead of default file LDS.
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      lds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      lds_cluster->set_name("lds_cluster");
      lds_cluster->mutable_typed_extension_protocol_options()->clear();
      ConfigHelper::setHttp2(*lds_cluster);
    });

    // In case to configure both HTTP and Listener ECDS filters, adding the 2nd ECDS cluster.
    if (two_ecds_filters_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
    }

    // Must be the last since it nukes static listeners.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
      listener_config_.set_name(listener_name_);
      ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
      bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
      auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
      lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
      lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
      envoy::config::core::v3::GrpcService* grpc_service =
          lds_api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "lds_cluster", getLdsFakeUpstream().localAddress());
    });

    HttpIntegrationTest::initialize();
    test_server_->waitForCounter("listener_manager.lds.update_success", Ge(1));
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    registerTestServerPorts({"http"});
  }

  void resetConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  ~UpstreamHttpExtensionDiscoveryIntegrationTestBase() override {
    resetConnection(ecds_connection_);
    resetConnection(lds_connection_);
    resetConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create the extension config discovery upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
    // Create the listener config discovery upstream (fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
    if (two_ecds_filters_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  // Wait for ECDS stream.
  void waitForEcdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                         FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    ASSERT_TRUE(result);
    result = connection->waitForNewStream(*dispatcher_, stream);
    ASSERT_TRUE(result);
    stream->startGrpcStream();
  }

  void waitXdsStream() {
    // Wait for LDS stream.
    auto& lds_upstream = getLdsFakeUpstream();
    AssertionResult result = lds_upstream.waitForHttpConnection(*dispatcher_, lds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = lds_connection_->waitForNewStream(*dispatcher_, lds_stream_);
    RELEASE_ASSERT(result, result.message());
    lds_stream_->startGrpcStream();

    // Response with initial LDS.
    sendLdsResponse("initial");

    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_ecds_filters_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
  }

  void sendLdsResponse(const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TestTypeUrl::get().Listener);
    std::ignore = response.add_resources()->PackFrom(listener_config_);
    lds_stream_->sendGrpcMessage(response);
  }

  void sendXdsResponse(const std::string& name, const std::string& version, const std::string& key,
                       const std::string& val, bool ttl = false, bool second_connection = false) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);

    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(val);
    std::ignore = typed_config.mutable_typed_config()->PackFrom(configuration);

    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);
    std::ignore = resource.mutable_resource()->PackFrom(typed_config);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }

    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    std::ignore = response.add_resources()->PackFrom(resource);

    if (!second_connection) {
      ecds_stream_->sendGrpcMessage(response);
    } else {
      ecds2_stream_->sendGrpcMessage(response);
    }
  }

  // Utilities used for config dump.
  absl::string_view request(const std::string port_key, const std::string method,
                            const std::string endpoint, BufferingStreamDecoderPtr& response) {
    response = IntegrationUtil::makeSingleRequest(lookupPort(port_key), method, endpoint, "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    return response->headers().getStatusValue();
  }

  absl::string_view contentType(const BufferingStreamDecoderPtr& response) {
    const Http::HeaderEntry* entry = response->headers().ContentType();
    if (entry == nullptr) {
      return "(null)";
    }
    return entry->value().getStringView();
  }

  const std::string filter_name_ = "foo";
  bool two_ecds_filters_{false};
  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getLdsFakeUpstream() const { return *fake_upstreams_[2]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[3]; }

  // gRPC LDS set-up
  envoy::config::listener::v3::Listener listener_config_;
  std::string listener_name_{"testing-listener-0"};
  FakeHttpConnectionPtr lds_connection_{nullptr};
  FakeStreamPtr lds_stream_{nullptr};

  // gRPC ECDS set-up
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

class DynamicRouterOrClusterFiltersIntegrationTest
    : public GrpcClientIntegrationParamTestWithVariantUpstreamConfig,
      public UpstreamHttpExtensionDiscoveryIntegrationTestBase {
public:
  DynamicRouterOrClusterFiltersIntegrationTest()
      : UpstreamHttpExtensionDiscoveryIntegrationTestBase(
            DynamicRouterOrClusterFiltersIntegrationTest::ipVersion(), std::get<1>(GetParam())) {}

  Network::Address::IpVersion ipVersion() const override {
    return std::get<0>(std::get<0>(GetParam()));
  }
  Grpc::ClientType clientType() const override { return std::get<1>(std::get<0>(GetParam())); }
};

// All upstream HTTP filters that are applied to cluster config must apply default config
// without warming. Otherwise, clusters initialization enters deadlock.
TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Expect default headers before config is updated
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "test-val1");

  // Send 2nd config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val2");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(2));
  auto headers3 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers3, "test-val2");
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1", true);
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, "test-val1");

  // Wait for configuration expiry, the default configuration should be applied.
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(2));
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, default_header_value_);
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicWithConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send config update with invalid config (header value length has to >=2).
  sendXdsResponse(filter_name_, "1", default_header_key_, "x");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_fail", Eq(1));
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_value_);
};

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, "test-val,test-val");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoSubscriptionsDifferentName) {
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", true, true, false, true);
  addCodecFilter();
  initialize();

  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_ + "," + default_header_value_);

  // Send 1st config update.
  sendXdsResponse("foo", "1", "header-key1", "test-val1");
  sendXdsResponse("bar", "1", "header-key2", "test-val1", false, true);
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.foo.config_reload",
                               Eq(1));
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.bar.config_reload",
                               Eq(1));
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "header-key1", "test-val1");
  expectHeaderKeyAndValue(headers2, "header-key2", "test-val1");

  // Send 2nd config update.
  sendXdsResponse("foo", "2", "header-key1", "test-val2");
  sendXdsResponse("bar", "2", "header-key2", "test-val2", false, true);
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.foo.config_reload",
                               Eq(2));
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.bar.config_reload",
                               Eq(2));
  auto headers3 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers3, "header-key1", "test-val2");
  expectHeaderKeyAndValue(headers3, "header-key2", "test-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addStaticFilter("bar", default_header_key_, "static-val1");
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", "header2", "static-val2");
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "xds-val,static-val1,xds-val");
  expectHeaderKeyAndValue(headers, "header2", "static-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", default_header_key_, "static-val1");
  addStaticFilter("foobar", "header2", "static-val2");
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers, default_header_key_, "static-val1,xds-val,xds-val");
  expectHeaderKeyAndValue(headers, "header2", "static-val2");
}

TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, UpdateDuringConnection) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response1 =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  std::unique_ptr<Http::RequestHeaderMap> headers1 =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(headers1 != nullptr);

  // Expect default headers before config is updated
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));

  IntegrationStreamDecoderPtr response2 =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  std::unique_ptr<Http::RequestHeaderMap> headers2 =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(headers2 != nullptr);

  // Expect default headers before config is updated
  expectHeaderKeyAndValue(headers2, "xds-val");

  cleanupUpstreamAndDownstream();
};

// Basic ECDS config dump test with one filter.
TEST_P(DynamicRouterOrClusterFiltersIntegrationTest, BasicSuccessWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addCodecFilter();
  initialize();

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "xds-val");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body()).value();
  size_t index = 0;
  auto array = json->getObjectArray("configs").value();
  for (const Json::ObjectSharedPtr& obj_ptr : array) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type").value()) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(7, config_dump.configs_size());

  // With /config_dump, the response has the format: EcdsConfigDump.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  std::ignore = config_dump.configs(2).UnpackTo(&ecds_config_dump);
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  test::integration::filters::AddHeaderFilterConfig http_filter_config;
  std::ignore = filter_config.typed_config().UnpackTo(&http_filter_config);
  EXPECT_EQ(default_header_key_, http_filter_config.header_key());
  EXPECT_EQ("xds-val", http_filter_config.header_value());
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientType, DynamicRouterOrClusterFiltersIntegrationTest,
    testing::Combine(GRPC_CLIENT_INTEGRATION_PARAMS, testing::Bool()),
    GrpcClientIntegrationParamTestWithVariantUpstreamConfig::protocolTestParamsToString);

class GrpcClientIntegrationParamTestParams
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Grpc::ClientType>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Grpc::ClientType>>&
          p) {
    return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                            : "EnvoyGrpc");
  }
};

class DynamicRouterAndClusterFiltersIntegrationTest
    : public GrpcClientIntegrationParamTestParams,
      public UpstreamHttpExtensionDiscoveryIntegrationTestBase {
public:
  DynamicRouterAndClusterFiltersIntegrationTest()
      : UpstreamHttpExtensionDiscoveryIntegrationTestBase(
            DynamicRouterAndClusterFiltersIntegrationTest::ipVersion(), false) {}

  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
};

TEST_P(DynamicRouterAndClusterFiltersIntegrationTest, DynamicRouterAndClusterSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicClusterFilter(filter_name_, true);
  addDynamicRouterFilter(filter_name_, true);
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  // Expect default headers before config is updated.
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse(filter_name_, "1", default_header_key_, "test-val1");
  test_server_->waitForCounter(
      "extension_config_discovery.upstream_http_filter." + filter_name_ + ".config_reload", Eq(1));
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "test-val1");
};

TEST_P(DynamicRouterAndClusterFiltersIntegrationTest, DynamicRouterAndClusterDifferentName) {
  two_ecds_filters_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicClusterFilter("foo", true);
  addDynamicRouterFilter("bar", true, true, false, true);
  addCodecClusterFilter();
  addCodecRouterFilter();
  initialize();

  // Expect default headers before config is updated.
  auto headers1 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers1, default_header_value_);

  // Send 1st config update.
  sendXdsResponse("foo", "1", default_header_key_, "value-from-cluster");
  sendXdsResponse("bar", "1", default_header_key_, "value-from-router", false, true);
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.foo.config_reload",
                               Eq(1));
  test_server_->waitForCounter("extension_config_discovery.upstream_http_filter.bar.config_reload",
                               Eq(1));
  auto headers2 = sendRequestAndGetHeaders();
  expectHeaderKeyAndValue(headers2, "value-from-cluster");
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, DynamicRouterAndClusterFiltersIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         GrpcClientIntegrationParamTestParams::protocolTestParamsToString);

} // namespace
} // namespace Envoy
