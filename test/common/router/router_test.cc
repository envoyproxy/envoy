#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/upstreams/http/http/v3/http_connection_pool.pb.h"
#include "envoy/extensions/upstreams/http/tcp/v3/tcp_connection_pool.pb.h"
#include "envoy/extensions/upstreams/tcp/generic/v3/generic_connection_pool.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/http/context_impl.h"
#include "common/network/application_protocol.h"
#include "common/network/socket_option_factory.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/network/utility.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/router.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/router/router_test_base.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::MockFunction;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {

class RouterTest : public RouterTestBase {
public:
  RouterTest() : RouterTestBase(false, false, Protobuf::RepeatedPtrField<std::string>{}) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  };
};

TEST_F(RouterTest, UpdateServerNameFilterState) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto dummy_option = absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>();
  dummy_option.value().set_auto_sni(true);
  ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, upstreamHttpProtocolOptions())
      .WillByDefault(ReturnRef(dummy_option));
  ON_CALL(callbacks_.stream_info_, filterState())
      .WillByDefault(ReturnRef(stream_info.filterState()));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  stream_info.filterState()->setData(Network::UpstreamServerName::key(),
                                     std::make_unique<Network::UpstreamServerName>("dummy"),
                                     StreamInfo::FilterState::StateType::Mutable);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;

  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ("host",
            stream_info.filterState()
                ->getDataReadOnly<Network::UpstreamServerName>(Network::UpstreamServerName::key())
                .value());
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, UpdateSubjectAltNamesFilterState) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto dummy_option = absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>();
  dummy_option.value().set_auto_san_validation(true);
  ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, upstreamHttpProtocolOptions())
      .WillByDefault(ReturnRef(dummy_option));
  ON_CALL(callbacks_.stream_info_, filterState())
      .WillByDefault(ReturnRef(stream_info.filterState()));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;

  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ("host", stream_info.filterState()
                        ->getDataReadOnly<Network::UpstreamSubjectAltNames>(
                            Network::UpstreamSubjectAltNames::key())
                        .value()[0]);
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RouteNotFound) {
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));

  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1UL, stats_store_.counter("test.no_route").value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(), "route_not_found");
}

TEST_F(RouterTest, MissingRequiredHeaders) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.removeMethod();

  EXPECT_CALL(encoder, encodeHeaders(_, _))
      .WillOnce(Invoke([](const Http::RequestHeaderMap& headers, bool) -> Http::Status {
        return Http::HeaderUtility::checkRequiredHeaders(headers);
      }));
  EXPECT_CALL(callbacks_,
              sendLocalReply(Http::Code::ServiceUnavailable,
                             testing::Eq("missing required header: :method"), _, _,
                             "filter_removed_required_headers{missing required header: :method}"))
      .WillOnce(testing::InvokeWithoutArgs([] {}));
  router_.decodeHeaders(headers, true);
  router_.onDestroy();
}

TEST_F(RouterTest, ClusterNotFound) {
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  ON_CALL(cm_, getThreadLocalCluster(_)).WillByDefault(Return(nullptr));
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1UL, stats_store_.counter("test.no_cluster").value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(), "cluster_not_found");
}

TEST_F(RouterTest, PoolFailureWithPriority) {
  ON_CALL(callbacks_.route_->route_entry_, priority())
      .WillByDefault(Return(Upstream::ResourcePriority::High));
  EXPECT_CALL(cm_.thread_local_cluster_,
              httpConnPool(Upstream::ResourcePriority::High, _, &router_));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                "tls version mismatch", cm_.thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      }));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "139"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // Pool failure, so upstream request was not initiated.
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(),
            "upstream_reset_before_response_started{connection failure,tls version mismatch}");
}

TEST_F(RouterTest, PoolFailureDueToConnectTimeout) {
  ON_CALL(callbacks_.route_->route_entry_, priority())
      .WillByDefault(Return(Upstream::ResourcePriority::High));
  EXPECT_CALL(cm_.thread_local_cluster_,
              httpConnPool(Upstream::ResourcePriority::High, _, &router_));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Timeout, "connect_timeout",
                                cm_.thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      }));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "134"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // Pool failure, so upstream request was not initiated.
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(),
            "upstream_reset_before_response_started{connection failure,connect_timeout}");
}

TEST_F(RouterTest, PoolFailureDueToConnectTimeoutLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure", "false"}});
  ON_CALL(callbacks_.route_->route_entry_, priority())
      .WillByDefault(Return(Upstream::ResourcePriority::High));
  EXPECT_CALL(cm_.thread_local_cluster_,
              httpConnPool(Upstream::ResourcePriority::High, _, &router_));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Timeout, "connect_timeout",
                                cm_.thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      }));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "127"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::LocalReset));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // Pool failure, so upstream request was not initiated.
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(),
            "upstream_reset_before_response_started{local reset,connect_timeout}");
}

TEST_F(RouterTest, Http1Upstream) {
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, absl::optional<Http::Protocol>(), _));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.route_->route_entry_, finalizeRequestHeaders(_, _, true));
  EXPECT_CALL(span_, injectContext(_));
  router_.decodeHeaders(headers, true);
  EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, Http2Upstream) {
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, absl::optional<Http::Protocol>(), _));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(span_, injectContext(_));
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, HashPolicy) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.hash_policy_));
  EXPECT_CALL(callbacks_.route_->route_entry_.hash_policy_, generateHash(_, _, _, _))
      .WillOnce(Return(absl::optional<uint64_t>(10)));
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(10UL, context->computeHashKey().value());
            return &cm_.thread_local_cluster_.conn_pool_;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, HashPolicyNoHash) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.hash_policy_));
  EXPECT_CALL(callbacks_.route_->route_entry_.hash_policy_, generateHash(_, _, _, _))
      .WillOnce(Return(absl::optional<uint64_t>()));
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, &router_))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_FALSE(context->computeHashKey());
            return &cm_.thread_local_cluster_.conn_pool_;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, HashKeyNoHashPolicy) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy()).WillByDefault(Return(nullptr));
  EXPECT_FALSE(router_.computeHashKey().has_value());
}

TEST_F(RouterTest, AddCookie) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.hash_policy_));
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(10UL, context->computeHashKey().value());
            return &cm_.thread_local_cluster_.conn_pool_;
          }));

  std::string cookie_value;
  EXPECT_CALL(callbacks_.route_->route_entry_.hash_policy_, generateHash(_, _, _, _))
      .WillOnce(Invoke([&](const Network::Address::Instance*, const Http::HeaderMap&,
                           const Http::HashPolicy::AddCookieCallback add_cookie,
                           const StreamInfo::FilterStateSharedPtr) {
        cookie_value = add_cookie("foo", "", std::chrono::seconds(1337));
        return absl::optional<uint64_t>(10);
      }));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, const bool) -> void {
        EXPECT_EQ(
            std::string{headers.get(Http::Headers::get().SetCookie)[0]->value().getStringView()},
            "foo=\"" + cookie_value + "\"; Max-Age=1337; HttpOnly");
      }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_EQ(callbacks_.details(), "via_upstream");
  // When the router filter gets reset we should cancel the pool request.
  router_.onDestroy();
}

TEST_F(RouterTest, AddCookieNoDuplicate) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.hash_policy_));
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(10UL, context->computeHashKey().value());
            return &cm_.thread_local_cluster_.conn_pool_;
          }));

  EXPECT_CALL(callbacks_.route_->route_entry_.hash_policy_, generateHash(_, _, _, _))
      .WillOnce(Invoke([&](const Network::Address::Instance*, const Http::HeaderMap&,
                           const Http::HashPolicy::AddCookieCallback add_cookie,
                           const StreamInfo::FilterStateSharedPtr) {
        // this should be ignored
        add_cookie("foo", "", std::chrono::seconds(1337));
        return absl::optional<uint64_t>(10);
      }));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, const bool) -> void {
        EXPECT_EQ(
            std::string{headers.get(Http::Headers::get().SetCookie)[0]->value().getStringView()},
            "foo=baz");
      }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"set-cookie", "foo=baz"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  // When the router filter gets reset we should cancel the pool request.
  router_.onDestroy();
}

TEST_F(RouterTest, AddMultipleCookies) {
  ON_CALL(callbacks_.route_->route_entry_, hashPolicy())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.hash_policy_));
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(10UL, context->computeHashKey().value());
            return &cm_.thread_local_cluster_.conn_pool_;
          }));

  std::string choco_c, foo_c;
  EXPECT_CALL(callbacks_.route_->route_entry_.hash_policy_, generateHash(_, _, _, _))
      .WillOnce(Invoke([&](const Network::Address::Instance*, const Http::HeaderMap&,
                           const Http::HashPolicy::AddCookieCallback add_cookie,
                           const StreamInfo::FilterStateSharedPtr) {
        choco_c = add_cookie("choco", "", std::chrono::seconds(15));
        foo_c = add_cookie("foo", "/path", std::chrono::seconds(1337));
        return absl::optional<uint64_t>(10);
      }));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, const bool) -> void {
        MockFunction<void(const std::string&)> cb;
        EXPECT_CALL(cb, Call("foo=\"" + foo_c + "\"; Max-Age=1337; Path=/path; HttpOnly"));
        EXPECT_CALL(cb, Call("choco=\"" + choco_c + "\"; Max-Age=15; HttpOnly"));

        headers.iterate([&cb](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          if (header.key() == Http::Headers::get().SetCookie.get()) {
            cb.Call(std::string(header.value().getStringView()));
          }
          return Http::HeaderMap::Iterate::Continue;
        });
      }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  router_.onDestroy();
}

TEST_F(RouterTest, MetadataNoOp) { EXPECT_EQ(nullptr, router_.metadataMatchCriteria()); }

TEST_F(RouterTest, MetadataMatchCriteria) {
  ON_CALL(callbacks_.route_->route_entry_, metadataMatchCriteria())
      .WillByDefault(Return(&callbacks_.route_->route_entry_.metadata_matches_criteria_));
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(context->metadataMatchCriteria(),
                      &callbacks_.route_->route_entry_.metadata_matches_criteria_);
            return &cm_.thread_local_cluster_.conn_pool_;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
}

TEST_F(RouterTest, MetadataMatchCriteriaFromRequest) {
  verifyMetadataMatchCriteriaFromRequest(true);
}

TEST_F(RouterTest, MetadataMatchCriteriaFromRequestNoRouteEntryMatch) {
  verifyMetadataMatchCriteriaFromRequest(false);
}

TEST_F(RouterTest, NoMetadataMatchCriteria) {
  ON_CALL(callbacks_.route_->route_entry_, metadataMatchCriteria()).WillByDefault(Return(nullptr));
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
            return &cm_.thread_local_cluster_.conn_pool_;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
}

TEST_F(RouterTest, CancelBeforeBoundToPool) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, NoHost) {
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(nullptr));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "19"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_maintenance_mode")
                    .value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(callbacks_.details(), "no_healthy_upstream");
}

TEST_F(RouterTest, MaintenanceMode) {
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, maintenanceMode()).WillOnce(Return(true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"},
                                                   {"content-length", "16"},
                                                   {"content-type", "text/plain"},
                                                   {"x-envoy-overloaded", "true"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow));
  EXPECT_CALL(span_, injectContext(_)).Times(0);

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_maintenance_mode")
                    .value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->load_report_stats_store_
                    .counter("upstream_rq_dropped")
                    .value());
  EXPECT_EQ(callbacks_.details(), "maintenance_mode");
}

TEST_F(RouterTest, ResponseCodeDetailsSetByUpstream) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate that x-envoy-upstream-service-time is added on a regular
// request/response path.
TEST_F(RouterTest, EnvoyUpstreamServiceTime) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool) {
        EXPECT_FALSE(headers.get(Http::Headers::get().EnvoyUpstreamServiceTime).empty());
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate that x-envoy-attempt-count is added to request headers when the option is true.
TEST_F(RouterTest, EnvoyAttemptCountInRequest) {
  verifyAttemptCountInRequestBasic(
      /* set_include_attempt_count_in_request */ true,
      /* preset_count*/ absl::nullopt,
      /* expected_count */ 1);
}

// Validate that x-envoy-attempt-count is overwritten by the router on request headers, if the
// header is sent from the downstream and the option is set to true.
TEST_F(RouterTest, EnvoyAttemptCountInRequestOverwritten) {
  verifyAttemptCountInRequestBasic(
      /* set_include_attempt_count_in_request */ true,
      /* preset_count*/ 123,
      /* expected_count */ 1);
}

// Validate that x-envoy-attempt-count is not overwritten by the router on request headers, if the
// header is sent from the downstream and the option is set to false.
TEST_F(RouterTest, EnvoyAttemptCountInRequestNotOverwritten) {
  verifyAttemptCountInRequestBasic(
      /* set_include_attempt_count_in_request */ false,
      /* preset_count*/ 123,
      /* expected_count */ 123);
}

TEST_F(RouterTest, EnvoyAttemptCountInRequestUpdatedInRetries) {
  setIncludeAttemptCountInRequest(true);

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Initial request has 1 attempt.
  EXPECT_EQ(1, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // The retry should cause the header to increase to 2.
  EXPECT_EQ(2, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Validate that x-envoy-attempt-count is added when option is true.
TEST_F(RouterTest, EnvoyAttemptCountInResponse) {
  verifyAttemptCountInResponseBasic(
      /* set_include_attempt_count_in_response */ true,
      /* preset_count */ absl::nullopt,
      /* expected_count */ 1);
}

// Validate that x-envoy-attempt-count is overwritten by the router on response headers, if the
// header is sent from the upstream and the option is set to true.
TEST_F(RouterTest, EnvoyAttemptCountInResponseOverwritten) {
  verifyAttemptCountInResponseBasic(
      /* set_include_attempt_count_in_response */ true,
      /* preset_count */ 123,
      /* expected_count */ 1);
}

// Validate that x-envoy-attempt-count is not overwritten by the router on response headers, if the
// header is sent from the upstream and the option is not set to true.
TEST_F(RouterTest, EnvoyAttemptCountInResponseNotOverwritten) {
  verifyAttemptCountInResponseBasic(
      /* set_include_attempt_count_in_response */ false,
      /* preset_count */ 123,
      /* expected_count */ 123);
}

// Validate that x-envoy-attempt-count is present in local replies after an upstream attempt is
// made.
TEST_F(RouterTest, EnvoyAttemptCountInResponsePresentWithLocalReply) {
  setIncludeAttemptCountInResponse(true);

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                absl::string_view(), cm_.thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      }));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"},
                                                   {"content-length", "91"},
                                                   {"content-type", "text/plain"},
                                                   {"x-envoy-attempt-count", "1"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  // Pool failure, so upstream request was never initiated.
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  EXPECT_EQ(callbacks_.details(), "upstream_reset_before_response_started{connection failure}");
}

// Validate that the x-envoy-attempt-count header in the downstream response reflects the number of
// of upstream requests that occurred when retries take place.
TEST_F(RouterTest, EnvoyAttemptCountInResponseWithRetries) {
  setIncludeAttemptCountInResponse(true);

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::ResponseHeaderMap& headers, bool) {
        // Because a retry happened the number of attempts in the response headers should be 2.
        EXPECT_EQ(2, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));
      }));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Append cluster with default header name.
TEST_F(RouterTest, AppendCluster0) { testAppendCluster(absl::nullopt); }

// Append cluster with custom header name.
TEST_F(RouterTest, AppendCluster1) {
  testAppendCluster(absl::make_optional(Http::LowerCaseString("x-custom-cluster")));
}

// Append hostname and address with default header names.
TEST_F(RouterTest, AppendUpstreamHost00) { testAppendUpstreamHost(absl::nullopt, absl::nullopt); }

// Append hostname and address with custom host address header name.
TEST_F(RouterTest, AppendUpstreamHost01) {
  testAppendUpstreamHost(
      absl::nullopt, absl::make_optional(Http::LowerCaseString("x-custom-upstream-host-address")));
}

// Append hostname and address with custom hostname header name.
TEST_F(RouterTest, AppendUpstreamHost10) {
  testAppendUpstreamHost(absl::make_optional(Http::LowerCaseString("x-custom-upstream-hostname")),
                         absl::nullopt);
}

// Append hostname and address with custom header names.
TEST_F(RouterTest, AppendUpstreamHost11) {
  testAppendUpstreamHost(
      absl::make_optional(Http::LowerCaseString("x-custom-upstream-hostname")),
      absl::make_optional(Http::LowerCaseString("x-custom-upstream-host-address")));
}

// Do not forward, with default not-forwarded header name
TEST_F(RouterTest, DoNotForward0) { testDoNotForward(absl::nullopt); }

// Do not forward, with custom not-forwarded header name
TEST_F(RouterTest, DoNotForward1) {
  testDoNotForward(absl::make_optional(Http::LowerCaseString("x-custom-not-forwarded")));
}

// Validate that all DebugConfig options play nicely with each other.
TEST_F(RouterTest, AllDebugConfig) {
  auto debug_config = std::make_unique<DebugConfig>(
      /* append_cluster */ true,
      /* cluster_header */ absl::nullopt,
      /* append_upstream_host */ true,
      /* hostname_header */ absl::nullopt,
      /* host_address_header */ absl::nullopt,
      /* do_not_forward */ true,
      /* not_forwarded_header */ absl::nullopt);
  callbacks_.streamInfo().filterState()->setData(DebugConfig::key(), std::move(debug_config),
                                                 StreamInfo::FilterState::StateType::ReadOnly,
                                                 StreamInfo::FilterState::LifeSpan::FilterChain);
  cm_.thread_local_cluster_.conn_pool_.host_->hostname_ = "scooby.doo";

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "204"},
      {"x-envoy-cluster", "fake_cluster"},
      {"x-envoy-upstream-hostname", "scooby.doo"},
      {"x-envoy-upstream-host-address", "10.0.0.5:9211"},
      {"x-envoy-not-forwarded", "true"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, NoRetriesOverflow) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // RetryOverflow kicks in.
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _))
      .WillOnce(Return(RetryStatus::NoOverflow));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 2));
}

TEST_F(RouterTest, ResetDuringEncodeHeaders) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(callbacks_, removeDownstreamWatermarkCallbacks(_));
  EXPECT_CALL(callbacks_, addDownstreamWatermarkCallbacks(_));
  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const Http::HeaderMap&, bool) -> Http::Status {
        encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
        return Http::okStatus();
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  // First connection is successful and reset happens later on.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

TEST_F(RouterTest, UpstreamTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _)).Times(0);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  response_timeout_->invokeCallback();

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Verify the timeout budget histograms are filled out correctly when using a
// global and per-try timeout in a successful request.
TEST_F(RouterTest, TimeoutBudgetHistogramStat) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "400"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "200"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Global timeout budget used.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), 20ull));
  // Per-try budget used.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"),
                  40ull));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  test_time_.advanceTimeWait(std::chrono::milliseconds(80));
  response_decoder->decodeData(data, true);
}

// Verify the timeout budget histograms are filled out correctly when using a
// global and per-try timeout in a failed request.
TEST_F(RouterTest, TimeoutBudgetHistogramStatFailure) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "400"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "200"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Global timeout budget used.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), 20ull));
  // Per-try budget used.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"),
                  40ull));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  test_time_.advanceTimeWait(std::chrono::milliseconds(80));
  response_decoder->decodeData(data, true);
}

// Verify the timeout budget histograms are filled out correctly when only using a global timeout.
TEST_F(RouterTest, TimeoutBudgetHistogramStatOnlyGlobal) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "200"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Global timeout budget used.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), 40ull));
  // Per-try budget used is zero out of an infinite timeout.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"), 0ull));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  test_time_.advanceTimeWait(std::chrono::milliseconds(80));
  response_decoder->decodeData(data, true);
}

// Verify the timeout budget histograms are filled out correctly across retries.
TEST_F(RouterTest, TimeoutBudgetHistogramStatDuringRetries) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                         {"x-envoy-upstream-rq-timeout-ms", "400"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "100"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Per-try budget used on the first request.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"),
                  100ull));
  // Global timeout histogram does not fire on the first request.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), _))
      .Times(0);

  // Per-try timeout.
  test_time_.advanceTimeWait(std::chrono::milliseconds(100));
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "504"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(504));
  response_decoder1->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Per-try budget exhausted on the second try.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"),
                  100ull));
  // Global timeout percentage used across both tries.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), 50ull));

  // Trigger second request failure.
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder2.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  test_time_.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  per_try_timeout_->invokeCallback();

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_per_try_timeout")
                    .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 2));
}

// Verify the timeout budget histograms are filled out correctly when the global timeout occurs
// during a retry.
TEST_F(RouterTest, TimeoutBudgetHistogramStatDuringGlobalTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                         {"x-envoy-upstream-rq-timeout-ms", "400"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "320"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Per-try budget used on the first request.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"),
                  50ull));
  // Global timeout histogram does not fire on the first request.
  EXPECT_CALL(cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), _))
      .Times(0);

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  test_time_.advanceTimeWait(std::chrono::milliseconds(160));
  response_decoder1->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Global timeout was hit, fires 100.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_percent_used"), 100ull));
  // Per-try budget used on the second request won't fire because the global timeout was hit.
  EXPECT_CALL(
      cm_.thread_local_cluster_.cluster_.info_->timeout_budget_stats_store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "upstream_rq_timeout_budget_per_try_percent_used"), _))
      .Times(0);

  // Trigger global timeout.
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder2.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  test_time_.advanceTimeWait(std::chrono::milliseconds(240));
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _)).Times(0);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  response_timeout_->invokeCallback();

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 2));
}

// Validate gRPC OK response stats are sane when response is trailers only.
TEST_F(RouterTest, GrpcOkTrailersOnly) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate gRPC AlreadyExists response stats are sane when response is trailers only.
TEST_F(RouterTest, GrpcAlreadyExistsTrailersOnly) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "6"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(409));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate gRPC Unavailable response stats are sane when response is trailers only.
TEST_F(RouterTest, GrpcOutlierDetectionUnavailableStatusCode) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "14"}});
  // Outlier detector will use the gRPC response status code.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Validate gRPC Internal response stats are sane when response is trailers only.
TEST_F(RouterTest, GrpcInternalTrailersOnly) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "13"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Validate gRPC response stats are sane when response is ended in a DATA
// frame.
TEST_F(RouterTest, GrpcDataEndStream) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  Buffer::OwnedImpl data;
  response_decoder->decodeData(data, true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Validate gRPC response stats are sane when response is reset after initial
// response HEADERS.
TEST_F(RouterTest, GrpcReset) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  EXPECT_EQ(1UL, stats_store_.counter("test.rq_reset_after_downstream_response_started").value());
}

// Validate gRPC OK response stats are sane when response is not trailers only.
TEST_F(RouterTest, GrpcOk) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_));
  Http::ResponseTrailerMapPtr response_trailers(
      new Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});
  response_decoder->decodeTrailers(std::move(response_trailers));
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate gRPC Internal response stats are sane when response is not trailers only.
TEST_F(RouterTest, GrpcInternal) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  Http::ResponseTrailerMapPtr response_trailers(
      new Http::TestResponseTrailerMapImpl{{"grpc-status", "13"}});
  response_decoder->decodeTrailers(std::move(response_trailers));
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

TEST_F(RouterTest, UpstreamTimeoutWithAltResponse) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-alt-response", "204"},
                                         {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "204"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _)).Times(0);
  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(204)));
  response_timeout_->invokeCallback();

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Verifies that the per try timeout is initialized once the downstream request has been read.
TEST_F(RouterTest, UpstreamPerTryTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  // We verify that both timeouts are started after decodeData(_, true) is called. This
  // verifies that we are not starting the initial per try timeout on the first onPoolReady.FOO
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  per_try_timeout_->invokeCallback();

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_per_try_timeout")
                    .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Verifies that the per try timeout starts when onPoolReady is called when it occursFOO
// after the downstream request has been read.
TEST_F(RouterTest, UpstreamPerTryTimeoutDelayedPoolReady) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  Http::ConnectionPool::Callbacks* pool_callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            pool_callbacks = &callbacks;
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  // Global timeout starts when decodeData(_, true) is called.
  expectResponseTimerCreate();
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);

  // Per try timeout starts when onPoolReady is called.FOO
  expectPerTryTimerCreate();
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  pool_callbacks->onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, Http::Protocol::Http10);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  per_try_timeout_->invokeCallback();

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_per_try_timeout")
                    .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Ensures that the per try callback is not set until the stream becomes available.
TEST_F(RouterTest, UpstreamPerTryTimeoutExcludesNewStream) {
  InSequence s;
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  Http::ConnectionPool::Callbacks* pool_callbacks;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            pool_callbacks = &callbacks;
            return nullptr;
          }));

  response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*response_timeout_, enableTimer(_, _));

  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);

  per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*per_try_timeout_, enableTimer(_, _));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  // The per try timeout timer should not be started yet.
  pool_callbacks->onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, Http::Protocol::Http10);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(*per_try_timeout_, disableTimer());
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(*response_timeout_, disableTimer());
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  per_try_timeout_->invokeCallback();

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_per_try_timeout")
                    .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Tests that a retry is sent after the first request hits the per try timeout, but then
// headers received in response to the first request are still used (and the 2nd request
// canceled).
TEST_F(RouterTest, HedgedPerTryTimeoutFirstRequestSucceeds) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // We should not have updated any stats yet because no requests have been
  // canceled
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  // Now write a 200 back. We expect the 2nd stream to be reset and stats to be
  // incremented properly.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(encoder2.stream_, resetStream(_));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
        EXPECT_TRUE(end_stream);
      }));
  response_decoder1->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  // TODO: Verify hedge stats here once they are implemented.
}

// Tests that an upstream request is reset even if it can't be retried as long as there is
// another in-flight request we're waiting on.
// Sequence:
// 1) first upstream request per try timeout
// 2) second upstream request sent
// 3) second upstream request gets 5xx, retries exhausted, assert it's reset
// 4) first upstream request gets 2xx
TEST_F(RouterTest, HedgedPerTryTimeoutResetsOnBadHeaders) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // We should not have updated any stats yet because no requests have been
  // canceled
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  // Now write a 5xx back on the 2nd request with no retries remaining. The 2nd request
  // should be reset immediately.
  Http::ResponseHeaderMapPtr bad_response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(encoder2.stream_, resetStream(_));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _))
      .WillOnce(Return(RetryStatus::NoOverflow));
  // Not end_stream, otherwise we wouldn't need to reset.
  response_decoder2->decodeHeaders(std::move(bad_response_headers), false);

  // Now write a 200 back. We expect the 2nd stream to be reset and stats to be
  // incremented properly.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
        EXPECT_TRUE(end_stream);
      }));
  response_decoder1->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));

  // TODO: Verify hedge stats here once they are implemented.
}

// Three requests sent: 1) 5xx error, 2) per try timeout, 3) gets good response
// headers.
TEST_F(RouterTest, HedgedPerTryTimeoutThirdRequestSucceeds) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();
  expectPerTryTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);

  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  // Local origin connect success happens for first and third try.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  router_.retry_state_->expectHeadersRetry();
  response_decoder1->decodeHeaders(std::move(response_headers1), true);

  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Now trigger a per try timeout on the 2nd request, expect a 3rd
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  NiceMock<Http::MockRequestEncoder> encoder3;
  Http::ResponseDecoder* response_decoder3 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder3 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder3, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  per_try_timeout_->invokeCallback();
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(3U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Now write a 200 back. We expect the 2nd stream to be reset and stats to be
  // incremented properly.
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(encoder2.stream_, resetStream(_));
  EXPECT_CALL(encoder3.stream_, resetStream(_)).Times(0);

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  response_decoder3->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));

  // TODO: Verify hedge stats here once they are implemented.
}

// First request times out and is retried, and then a response is received.
// Make sure we don't attempt to retry because we already retried for timeout.
TEST_F(RouterTest, RetryOnlyOnceForSameUpstreamRequest) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  expectPerTryTimerCreate();
  router_.retry_state_->callback_();

  // Now send a 5xx back and make sure we don't ask whether we should retry it.
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).Times(0);
  EXPECT_CALL(*router_.retry_state_, wouldRetryFromHeaders(_)).WillOnce(Return(true));
  response_decoder1->decodeHeaders(std::move(response_headers1), true);

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));

  response_timeout_->invokeCallback();
}

// Sequence: upstream request hits soft per try timeout and is retried, and
// then "bad" response headers come back before the retry has been scheduled.
// Ensures that the "bad" headers are not sent downstream because there is
// still an attempt pending.
TEST_F(RouterTest, BadHeadersDroppedIfPreviousRetryScheduled) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  expectPerTryTimerCreate();

  // Now send a 5xx back and make sure we don't ask whether we should retry it
  // and also that we don't respond downstream with it.
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).Times(0);
  EXPECT_CALL(*router_.retry_state_, wouldRetryFromHeaders(_)).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  response_decoder1->decodeHeaders(std::move(response_headers1), true);

  // Now trigger the retry for the per try timeout earlier.
  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();

  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder2->decodeHeaders(std::move(response_headers2), true);
}

// Test retrying a request, when the first attempt fails before the client
// has sent any of the body.
TEST_F(RouterTest, RetryRequestBeforeBody) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(encoder2, encodeHeaders(HeaderHasValueRef("myheader", "present"), false));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Complete request. Ensure original headers are present.
  const std::string body("body");
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body), true));
  Buffer::OwnedImpl buf(body);
  router_.decodeData(buf, true);

  // Send successful response, verify success.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Test retrying a request, when the first attempt fails while the client
// is sending the body.
TEST_F(RouterTest, RetryRequestDuringBody) {
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(callbacks_, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) { decoding_buffer.move(data); }));

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  const std::string body1("body1");
  Buffer::OwnedImpl buf1(body1);
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  router_.decodeData(buf1, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(encoder2, encodeHeaders(HeaderHasValueRef("myheader", "present"), false));
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body1), false));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Complete request. Ensure original headers are present.
  const std::string body2("body2");
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body2), true));
  Buffer::OwnedImpl buf2(body2);
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  router_.decodeData(buf2, true);

  // Send successful response, verify success.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Test retrying a request, when the first attempt fails while the client
// is sending the body, with more data arriving in between upstream attempts
// (which would normally happen during the backoff timer interval), but not end_stream.
TEST_F(RouterTest, RetryRequestDuringBodyDataBetweenAttemptsNotEndStream) {
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(callbacks_, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) { decoding_buffer.move(data); }));

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  const std::string body1("body1");
  Buffer::OwnedImpl buf1(body1);
  EXPECT_CALL(*router_.retry_state_, enabled()).Times(3).WillRepeatedly(Return(true));
  router_.decodeData(buf1, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  const std::string body2("body2");
  Buffer::OwnedImpl buf2(body2);
  router_.decodeData(buf2, false);

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(encoder2, encodeHeaders(HeaderHasValueRef("myheader", "present"), false));
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body1 + body2), false));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Complete request. Ensure original headers are present.
  const std::string body3("body3");
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body3), true));
  Buffer::OwnedImpl buf3(body3);
  router_.decodeData(buf3, true);

  // Send successful response, verify success.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Test retrying a request, when the first attempt fails while the client
// is sending the body, with the rest of the request arriving in between upstream
// request attempts.
TEST_F(RouterTest, RetryRequestDuringBodyCompleteBetweenAttempts) {
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(callbacks_, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) { decoding_buffer.move(data); }));

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  const std::string body1("body1");
  Buffer::OwnedImpl buf1(body1);
  EXPECT_CALL(*router_.retry_state_, enabled()).Times(2).WillRepeatedly(Return(true));
  router_.decodeData(buf1, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // Complete request while there is no upstream request.
  const std::string body2("body2");
  Buffer::OwnedImpl buf2(body2);
  router_.decodeData(buf2, true);

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(encoder2, encodeHeaders(HeaderHasValueRef("myheader", "present"), false));
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body1 + body2), true));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Send successful response, verify success.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Test retrying a request, when the first attempt fails while the client
// is sending the body, with the trailers arriving in between upstream
// request attempts.
TEST_F(RouterTest, RetryRequestDuringBodyTrailerBetweenAttempts) {
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(callbacks_, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) { decoding_buffer.move(data); }));

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  const std::string body1("body1");
  Buffer::OwnedImpl buf1(body1);
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  router_.decodeData(buf1, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // Complete request while there is no upstream request.
  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  router_.decodeTrailers(trailers);

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(encoder2, encodeHeaders(HeaderHasValueRef("myheader", "present"), false));
  EXPECT_CALL(encoder2, encodeData(BufferStringEqual(body1), false));
  EXPECT_CALL(encoder2, encodeTrailers(HeaderMapEqualRef(&trailers)));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Send successful response, verify success.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Test retrying a request, when the first attempt fails while the client
// is sending the body, with the rest of the request arriving in between upstream
// request attempts, but exceeding the buffer limit causing a downstream request abort.
TEST_F(RouterTest, RetryRequestDuringBodyBufferLimitExceeded) {
  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(callbacks_, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) { decoding_buffer.move(data); }));
  EXPECT_CALL(callbacks_.route_->route_entry_, retryShadowBufferLimit()).WillOnce(Return(10));

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}, {"myheader", "present"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  const std::string body1("body1");
  Buffer::OwnedImpl buf1(body1);
  EXPECT_CALL(*router_.retry_state_, enabled()).Times(2).WillRepeatedly(Return(true));
  router_.decodeData(buf1, false);

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // Complete request while there is no upstream request.
  const std::string body2(50, 'a');
  Buffer::OwnedImpl buf2(body2);
  router_.decodeData(buf2, false);

  EXPECT_EQ(callbacks_.details(), "request_payload_exceeded_retry_buffer_limit");
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("retry_or_shadow_abandoned")
                    .value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

// Two requests are sent (slow request + hedged retry) and then global timeout
// is hit. Verify everything gets cleaned up.
TEST_F(RouterTest, HedgedPerTryTimeoutGlobalTimeout) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  // Now trigger global timeout, expect everything to be reset
  EXPECT_CALL(encoder1.stream_, resetStream(_));
  EXPECT_CALL(encoder2.stream_, resetStream(_));
  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "504");
      }));
  response_timeout_->invokeCallback();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 2));
  EXPECT_EQ(2, cm_.thread_local_cluster_.conn_pool_.host_->stats_.rq_timeout_.value());
  // TODO: Verify hedge stats here once they are implemented.
}

// Sequence: 1) per try timeout w/ hedge retry, 2) second request gets a 5xx
// response, no retries remaining 3) first request gets a 5xx response.
TEST_F(RouterTest, HedgingRetriesExhaustedBadResponse) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  // Now trigger a 503 in response to the second request.
  Http::ResponseHeaderMapPtr bad_response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));

  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _))
      .WillOnce(Return(RetryStatus::NoRetryLimitExceeded));
  response_decoder2->decodeHeaders(std::move(bad_response_headers1), true);

  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Now trigger a 502 in response to the first request.
  Http::ResponseHeaderMapPtr bad_response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "502"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(502));

  // We should not call shouldRetryHeaders() because you never retry the same
  // request twice.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).Times(0);

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "502");
      }));
  response_decoder1->decodeHeaders(std::move(bad_response_headers2), true);

  EXPECT_TRUE(verifyHostUpstreamStats(0, 2));
}

// Sequence: 1) per try timeout w/ hedge retry, 2) first request gets reset by upstream,
// 3) 2nd request gets a 200 which should be sent downstream.
TEST_F(RouterTest, HedgingRetriesProceedAfterReset) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder1 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder1 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  // First is reset
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)))
      .Times(2);
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  per_try_timeout_->invokeCallback();

  NiceMock<Http::MockRequestEncoder> encoder2;
  Http::ResponseDecoder* response_decoder2 = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder2 = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));

  // Now trigger an upstream reset in response to the first request.
  EXPECT_CALL(encoder1.stream_, resetStream(_));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We should not call shouldRetryReset() because you never retry the same
  // request twice.
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _)).Times(0);

  // Now trigger a 200 in response to the second request.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});

  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder2->decodeHeaders(std::move(response_headers), true);

  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Sequence: 1) request with data hits per try timeout w/ hedge retry, 2)
// second request is immediately reset 3) 1st request gets a 200.
// The goal of this test is to ensure that the router can properly detect that an immediate
// reset happens and that we don't accidentally write data twice on the first request.
TEST_F(RouterTest, HedgingRetryImmediatelyReset) {
  enableHedgeOnPerTryTimeout();

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                        absl::optional<uint64_t>(absl::nullopt)));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  expectPerTryTimerCreate();
  expectResponseTimerCreate();
  Buffer::OwnedImpl body("test body");
  EXPECT_CALL(encoder, encodeData(_, _));
  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("hello"));
  router_.retry_state_->expectHedgedPerTryTimeoutRetry();
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, router_.decodeData(*body_data, true));

  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, absl::optional<uint64_t>(504)));
  EXPECT_CALL(encoder.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  per_try_timeout_->invokeCallback();

  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
        EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                    putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                absl::string_view(), cm_.thread_local_cluster_.conn_pool_.host_);
        return nullptr;
      }));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _))
      .WillOnce(Return(RetryStatus::NoRetryLimitExceeded));
  ON_CALL(callbacks_, decodingBuffer()).WillByDefault(Return(body_data.get()));
  router_.retry_state_->callback_();

  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Now trigger a 200 in response to the first request.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});

  // The request was already retried when the per try timeout occurred so it
  // should't even consult the retry state.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).Times(0);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value(), "200");
      }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);

  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
  // Pool failure for the first try, so only 1 upstream request was made.
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryNoneHealthy) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  expectResponseTimerCreate();
  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  router_.retry_state_->expectResetRetry();
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::LocalReset);

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _)).WillOnce(Return(nullptr));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "19"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream));
  router_.retry_state_->callback_();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // Pool failure for the first try, so only 1 upstream request was made.
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryUpstreamReset) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, addDecodedData(_, _));
  Buffer::OwnedImpl body("test body");
  router_.decodeData(body, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  router_.retry_state_->expectResetRetry();
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // We expect this reset to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                        putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                                  absl::optional<uint64_t>(absl::nullopt)));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

TEST_F(RouterTest, NoRetryWithBodyLimit) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  // Set a per route body limit which disallows any buffering.
  EXPECT_CALL(callbacks_.route_->route_entry_, retryShadowBufferLimit()).WillOnce(Return(0));
  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  // Unlike RetryUpstreamReset above the data won't be buffered as the body exceeds the buffer limit
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, addDecodedData(_, _)).Times(0);
  Buffer::OwnedImpl body("t");
  router_.decodeData(body, false);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

// Verifies that when the request fails with an upstream reset (per try timeout in this case)
// before an upstream host has been established, then the onHostAttempted function will not be
// invoked. This ensures that we're not passing a null host to the retry plugins.
TEST_F(RouterTest, RetryUpstreamPerTryTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                         {"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  router_.retry_state_->expectResetRetry();
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  per_try_timeout_->invokeCallback();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect this reset to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                        putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess,
                                  absl::optional<uint64_t>(absl::nullopt)));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Asserts that onHostAttempted is *not* called when the upstream connection fails in such
// a way that no host is present.
TEST_F(RouterTest, RetryUpstreamConnectionFailure) {
  Http::ConnectionPool::Callbacks* conn_pool_callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        conn_pool_callbacks = &callbacks;
        return nullptr;
      }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  EXPECT_CALL(*router_.retry_state_, onHostAttempted(_)).Times(0);

  router_.retry_state_->expectResetRetry();

  conn_pool_callbacks->onPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                     absl::string_view(), nullptr);
  // Pool failure, so no upstream request was made.
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseDecoder* response_decoder = nullptr;
  // We expect this reset to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

TEST_F(RouterTest, DontResetStartedResponseOnUpstreamPerTryTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectPerTryTimerCreate();
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Since the response is already started we don't retry.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  Buffer::OwnedImpl body("test body");
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  per_try_timeout_->invokeCallback();
  EXPECT_CALL(callbacks_, encodeData(_, true));
  response_decoder->decodeData(body, true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
  EXPECT_EQ(0U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_rq_per_try_timeout")
                    .value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryUpstreamResetResponseStarted) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Since the response is already started we don't retry.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), false);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  // Normally, sendLocalReply will actually send the reply, but in this case the
  // HCM will detect the headers have already been sent and not route through
  // the encoder again.
  EXPECT_CALL(callbacks_, sendLocalReply(_, _, _, _, _)).WillOnce(testing::InvokeWithoutArgs([] {
  }));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  // For normal HTTP, once we have a 200 we consider this a success, even if a
  // later reset occurs.
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

// The router filter is responsible for not propagating 100-continue headers after the initial 100.
TEST_F(RouterTest, Coalesce100ContinueHeaders) {
  // Setup.
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Initial 100-continue, this is processed normally.
  EXPECT_CALL(callbacks_, encode100ContinueHeaders_(_));
  {
    Http::ResponseHeaderMapPtr continue_headers(
        new Http::TestResponseHeaderMapImpl{{":status", "100"}});
    response_decoder->decode100ContinueHeaders(std::move(continue_headers));
  }
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_100").value());

  // No encode100ContinueHeaders() invocation for the second 100-continue (but we continue to track
  // stats from upstream).
  EXPECT_CALL(callbacks_, encode100ContinueHeaders_(_)).Times(0);
  {
    Http::ResponseHeaderMapPtr continue_headers(
        new Http::TestResponseHeaderMapImpl{{":status", "100"}});
    response_decoder->decode100ContinueHeaders(std::move(continue_headers));
  }
  EXPECT_EQ(
      2U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_100").value());

  // Reset stream and cleanup.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryUpstreamReset100ContinueResponseStarted) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // The 100-continue will result in resetting retry_state_, so when the stream
  // is reset we won't even check shouldRetryReset() (or shouldRetryHeaders()).
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _)).Times(0);
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).Times(0);
  EXPECT_CALL(callbacks_, encode100ContinueHeaders_(_));
  Http::ResponseHeaderMapPtr continue_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "100"}});
  response_decoder->decode100ContinueHeaders(std::move(continue_headers));
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_100").value());
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryUpstream5xx) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

TEST_F(RouterTest, RetryTimeoutDuringRetryDelay) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // Fire timeout.
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_, putResponseTime(_))
      .Times(0);
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  response_timeout_->invokeCallback();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
}

TEST_F(RouterTest, MaxStreamDurationValidlyConfiguredWithoutRetryPolicy) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  setUpstreamMaxStreamDuration(500);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectMaxStreamDurationTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  max_stream_duration_timer_->invokeCallback();

  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, MaxStreamDurationDisabledIfSetToZero) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  setUpstreamMaxStreamDuration(0);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  // not to be called timer creation.
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_).Times(0);

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, MaxStreamDurationCallbackNotCalled) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  setUpstreamMaxStreamDuration(5000);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectMaxStreamDurationTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, MaxStreamDurationWhenDownstreamAlreadyStartedWithoutRetryPolicy) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  setUpstreamMaxStreamDuration(500);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectMaxStreamDurationTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  max_stream_duration_timer_->invokeCallback();

  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

TEST_F(RouterTest, MaxStreamDurationWithRetryPolicy) {
  // First upstream request
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  setUpstreamMaxStreamDuration(500);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectMaxStreamDurationTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "reset"},
                                         {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.retry_state_->expectResetRetry();
  max_stream_duration_timer_->invokeCallback();

  // Second upstream request
  NiceMock<Http::MockRequestEncoder> encoder2;
  setUpstreamMaxStreamDuration(500);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectMaxStreamDurationTimerCreate();
  router_.retry_state_->callback_();

  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

TEST_F(RouterTest, RetryTimeoutDuringRetryDelayWithUpstreamRequestNoHost) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  Envoy::ConnectionPool::MockCancellable cancellable;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::ResponseDecoder& decoder,
                           Http::ConnectionPool::Callbacks&) -> Http::ConnectionPool::Cancellable* {
        response_decoder = &decoder;
        return &cancellable;
      }));
  router_.retry_state_->callback_();

  // Fire timeout.
  EXPECT_CALL(cancellable, cancel(_));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_, putResponseTime(_))
      .Times(0);
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  response_timeout_->invokeCallback();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // Timeout fired so no retry was done.
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

// Retry timeout during a retry delay leading to no upstream host, as well as an alt response code.
TEST_F(RouterTest, RetryTimeoutDuringRetryDelayWithUpstreamRequestNoHostAltResponseCode) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                         {"x-envoy-internal", "true"},
                                         {"x-envoy-upstream-rq-timeout-alt-response", "204"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  Envoy::ConnectionPool::MockCancellable cancellable;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::ResponseDecoder& decoder,
                           Http::ConnectionPool::Callbacks&) -> Http::ConnectionPool::Cancellable* {
        response_decoder = &decoder;
        return &cancellable;
      }));
  router_.retry_state_->callback_();

  // Fire timeout.
  EXPECT_CALL(cancellable, cancel(_));
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_, putResponseTime(_))
      .Times(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "204"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  response_timeout_->invokeCallback();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));
  // no retry was done.
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, RetryUpstream5xxNotComplete) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("hello"));
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, addDecodedData(_, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, router_.decodeData(*body_data, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  router_.decodeTrailers(trailers);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(encoder1.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  ON_CALL(callbacks_, decodingBuffer()).WillByDefault(Return(body_data.get()));
  EXPECT_CALL(encoder2, encodeHeaders(_, false));
  EXPECT_CALL(encoder2, encodeData(_, false));
  EXPECT_CALL(encoder2, encodeTrailers(_));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_, putResponseTime(_));
  EXPECT_CALL(
      cm_.thread_local_cluster_.conn_pool_.host_->health_checker_,
      setUnhealthy(Upstream::HealthCheckHostMonitor::UnhealthyType::ImmediateHealthCheckFail));
  Http::ResponseHeaderMapPtr response_headers2(new Http::TestResponseHeaderMapImpl{
      {":status", "200"}, {"x-envoy-immediate-health-check-fail", "true"}});
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("retry.upstream_rq_503")
                .value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("zone.zone_name.to_az.upstream_rq_200")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("zone.zone_name.to_az.upstream_rq_2xx")
                    .value());
}

// Validate gRPC Cancelled response stats are sane when retry is taking effect.
TEST_F(RouterTest, RetryUpstreamGrpcCancelled) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-grpc-on", "cancelled"},
                                         {"x-envoy-internal", "true"},
                                         {"content-type", "application/grpc"},
                                         {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // gRPC with status "cancelled" (1)
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(499));
  response_decoder->decodeHeaders(std::move(response_headers1), true);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the grpc-status to result in a retried request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Verifies that the initial host is select with max host count of one, but during retries
// RetryPolicy will be consulted.
TEST_F(RouterTest, RetryRespsectsMaxHostSelectionCount) {
  router_.reject_all_hosts_ = true;

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  ON_CALL(*router_.retry_state_, hostSelectionMaxAttempts()).WillByDefault(Return(3));
  // The router should accept any host at this point, since we're not in a retry.
  EXPECT_EQ(1, router_.hostSelectionRetryCount());

  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("hello"));
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, addDecodedData(_, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, router_.decodeData(*body_data, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  router_.decodeTrailers(trailers);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(encoder1.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  ON_CALL(callbacks_, decodingBuffer()).WillByDefault(Return(body_data.get()));
  EXPECT_CALL(encoder2, encodeHeaders(_, false));
  EXPECT_CALL(encoder2, encodeData(_, false));
  EXPECT_CALL(encoder2, encodeTrailers(_));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Now that we're triggered a retry, we should see the configured number of host selections.
  EXPECT_EQ(3, router_.hostSelectionRetryCount());

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

// Verifies that the initial request accepts any host, but during retries
// RetryPolicy will be consulted.
TEST_F(RouterTest, RetryRespectsRetryHostPredicate) {
  router_.reject_all_hosts_ = true;

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  NiceMock<Upstream::MockHost> host;
  // The router should accept any host at this point, since we're not in a retry.
  EXPECT_FALSE(router_.shouldSelectAnotherHost(host));

  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("hello"));
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, addDecodedData(_, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, router_.decodeData(*body_data, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  router_.decodeTrailers(trailers);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // 5xx response.
  router_.retry_state_->expectHeadersRetry();
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(encoder1.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(503));
  response_decoder->decodeHeaders(std::move(response_headers1), false);
  EXPECT_TRUE(verifyHostUpstreamStats(0, 1));

  // We expect the 5xx response to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  ON_CALL(callbacks_, decodingBuffer()).WillByDefault(Return(body_data.get()));
  EXPECT_CALL(encoder2, encodeHeaders(_, false));
  EXPECT_CALL(encoder2, encodeData(_, false));
  EXPECT_CALL(encoder2, encodeTrailers(_));
  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Now that we're triggered a retry, we should see the router reject hosts.
  EXPECT_TRUE(router_.shouldSelectAnotherHost(host));

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->health_checker_, setUnhealthy(_))
      .Times(0);
  Http::ResponseHeaderMapPtr response_headers2(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  response_decoder->decodeHeaders(std::move(response_headers2), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 1));
}

TEST_F(RouterTest, InternalRedirectRejectedWhenReachingMaxInternalRedirect) {
  enableRedirects(3);
  setNumPreviousRedirect(3);
  sendRequest();

  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);

  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
  EXPECT_EQ(1UL,
            stats_store_.counter("test.passthrough_internal_redirect_too_many_redirects").value());
}

TEST_F(RouterTest, InternalRedirectRejectedWithEmptyLocation) {
  enableRedirects();
  sendRequest();

  redirect_headers_->setLocation("");

  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);

  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
  EXPECT_EQ(1UL, stats_store_.counter("test.passthrough_internal_redirect_bad_location").value());
}

TEST_F(RouterTest, InternalRedirectRejectedWithInvalidLocation) {
  enableRedirects();
  sendRequest();

  redirect_headers_->setLocation("h");

  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);

  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
  EXPECT_EQ(1UL, stats_store_.counter("test.passthrough_internal_redirect_bad_location").value());
}

TEST_F(RouterTest, InternalRedirectRejectedWithoutCompleteRequest) {
  enableRedirects();

  sendRequest(false);

  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);

  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
}

TEST_F(RouterTest, InternalRedirectRejectedWithoutLocation) {
  enableRedirects();

  sendRequest();

  redirect_headers_->removeLocation();

  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);
  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
}

TEST_F(RouterTest, InternalRedirectRejectedWithBody) {
  enableRedirects();

  sendRequest();

  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("random_fake_data"));
  EXPECT_CALL(callbacks_, decodingBuffer()).WillOnce(Return(body_data.get()));
  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);
  Buffer::OwnedImpl data("1234567890");
  response_decoder_->decodeData(data, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
}

TEST_F(RouterTest, CrossSchemeRedirectRejectedByPolicy) {
  enableRedirects();

  sendRequest();

  redirect_headers_->setLocation("https://www.foo.com");

  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
  EXPECT_EQ(1UL, stats_store_.counter("test.passthrough_internal_redirect_unsafe_scheme").value());
}

TEST_F(RouterTest, InternalRedirectRejectedByPredicate) {
  enableRedirects();

  sendRequest();

  redirect_headers_->setLocation("http://www.foo.com/some/path");

  auto mock_predicate = std::make_shared<NiceMock<MockInternalRedirectPredicate>>();

  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(callbacks_, clearRouteCache());
  EXPECT_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, predicates())
      .WillOnce(Return(std::vector<InternalRedirectPredicateSharedPtr>({mock_predicate})));
  EXPECT_CALL(*mock_predicate, acceptTargetRoute(_, _, _, _)).WillOnce(Return(false));
  ON_CALL(*mock_predicate, name()).WillByDefault(Return("mock_predicate"));
  EXPECT_CALL(callbacks_, recreateStream(_)).Times(0);

  response_decoder_->decodeHeaders(std::move(redirect_headers_), true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_failed_total")
                    .value());
  EXPECT_EQ(1UL, stats_store_.counter("test.passthrough_internal_redirect_predicate").value());

  // Make sure the original host/path is preserved.
  EXPECT_EQ("host", default_request_headers_.getHostValue());
  EXPECT_EQ("/", default_request_headers_.getPathValue());
  // Make sure x-envoy-original-url is not set for unsuccessful redirect.
  EXPECT_EQ(nullptr, default_request_headers_.EnvoyOriginalUrl());
}

TEST_F(RouterTest, HttpInternalRedirectSucceeded) {
  enableRedirects(3);
  setNumPreviousRedirect(2);
  default_request_headers_.setForwardedProto("http");
  sendRequest();

  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(callbacks_, clearRouteCache());
  EXPECT_CALL(callbacks_, recreateStream(_)).WillOnce(Return(true));
  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_succeeded_total")
                    .value());

  // In production, the HCM recreateStream would have called this.
  router_.onDestroy();
  EXPECT_EQ(3, callbacks_.streamInfo()
                   .filterState()
                   ->getDataMutable<StreamInfo::UInt32Accessor>("num_internal_redirects")
                   .value());
}

TEST_F(RouterTest, HttpsInternalRedirectSucceeded) {
  auto ssl_connection = std::make_shared<Ssl::MockConnectionInfo>();
  enableRedirects(3);
  setNumPreviousRedirect(1);

  sendRequest();

  redirect_headers_->setLocation("https://www.foo.com");
  EXPECT_CALL(connection_, ssl()).WillOnce(Return(ssl_connection));
  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(callbacks_, clearRouteCache());
  EXPECT_CALL(callbacks_, recreateStream(_)).WillOnce(Return(true));
  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_succeeded_total")
                    .value());

  // In production, the HCM recreateStream would have called this.
  router_.onDestroy();
}

TEST_F(RouterTest, CrossSchemeRedirectAllowedByPolicy) {
  auto ssl_connection = std::make_shared<Ssl::MockConnectionInfo>();
  enableRedirects();

  sendRequest();

  redirect_headers_->setLocation("http://www.foo.com");
  EXPECT_CALL(connection_, ssl()).WillOnce(Return(ssl_connection));
  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_,
              isCrossSchemeRedirectAllowed())
      .WillOnce(Return(true));
  EXPECT_CALL(callbacks_, clearRouteCache());
  EXPECT_CALL(callbacks_, recreateStream(_)).WillOnce(Return(true));
  response_decoder_->decodeHeaders(std::move(redirect_headers_), false);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_internal_redirect_succeeded_total")
                    .value());

  // In production, the HCM recreateStream would have called this.
  router_.onDestroy();
}

TEST_F(RouterTest, Shadow) {
  ShadowPolicyPtr policy = std::make_unique<TestShadowPolicy>("foo", "bar");
  callbacks_.route_->route_entry_.shadow_policies_.push_back(std::move(policy));
  policy = std::make_unique<TestShadowPolicy>("fizz", "buzz", envoy::type::v3::FractionalPercent(),
                                              false);
  callbacks_.route_->route_entry_.shadow_policies_.push_back(std::move(policy));
  ON_CALL(callbacks_, streamId()).WillByDefault(Return(43));

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("bar", 0, 43, 10000)).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("buzz", 0, 43, 10000)).WillOnce(Return(true));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  Buffer::InstancePtr body_data(new Buffer::OwnedImpl("hello"));
  EXPECT_CALL(callbacks_, addDecodedData(_, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, router_.decodeData(*body_data, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_CALL(callbacks_, decodingBuffer())
      .Times(AtLeast(2))
      .WillRepeatedly(Return(body_data.get()));
  EXPECT_CALL(*shadow_writer_, shadow_("foo", _, _))
      .WillOnce(Invoke([](const std::string&, Http::RequestMessagePtr& request,
                          const Http::AsyncClient::RequestOptions& options) -> void {
        EXPECT_NE(request->body().length(), 0);
        EXPECT_NE(nullptr, request->trailers());
        EXPECT_EQ(absl::optional<std::chrono::milliseconds>(10), options.timeout);
        EXPECT_TRUE(options.sampled_);
      }));
  EXPECT_CALL(*shadow_writer_, shadow_("fizz", _, _))
      .WillOnce(Invoke([](const std::string&, Http::RequestMessagePtr& request,
                          const Http::AsyncClient::RequestOptions& options) -> void {
        EXPECT_NE(request->body().length(), 0);
        EXPECT_NE(nullptr, request->trailers());
        EXPECT_EQ(absl::optional<std::chrono::milliseconds>(10), options.timeout);
        EXPECT_FALSE(options.sampled_);
      }));
  router_.decodeTrailers(trailers);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

TEST_F(RouterTest, AltStatName) {
  // Also test no upstream timeout here.
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-alt-stat-name", "alt_stat"},
                                         {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_, putResponseTime(_));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"},
                                          {"x-envoy-upstream-canary", "true"},
                                          {"x-envoy-virtual-cluster", "hello"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  EXPECT_EQ(1U,
            stats_store_.counter("vhost.fake_vhost.vcluster.fake_virtual_cluster.upstream_rq_200")
                .value());
  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("canary.upstream_rq_200")
                .value());
  EXPECT_EQ(
      1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("alt_stat.upstream_rq_200")
              .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("alt_stat.zone.zone_name.to_az.upstream_rq_200")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("alt_stat.zone.zone_name.to_az.upstream_rq_200")
                    .value());
}

TEST_F(RouterTest, Redirect) {
  MockDirectResponseEntry direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, newPath(_)).WillOnce(Return("hello"));
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, rewritePathHeader(_, _));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::MovedPermanently));
  EXPECT_CALL(direct_response, responseBody()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(direct_response, finalizeResponseHeaders(_, _));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "301"}, {"location", "hello"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, RedirectFound) {
  MockDirectResponseEntry direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, newPath(_)).WillOnce(Return("hello"));
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, rewritePathHeader(_, _));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::Found));
  EXPECT_CALL(direct_response, responseBody()).WillOnce(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(direct_response, finalizeResponseHeaders(_, _));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "302"}, {"location", "hello"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

TEST_F(RouterTest, DirectResponse) {
  NiceMock<MockDirectResponseEntry> direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::OK));
  EXPECT_CALL(direct_response, responseBody()).WillRepeatedly(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(span_, injectContext(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(1UL, config_.stats_.rq_direct_response_.value());
}

TEST_F(RouterTest, DirectResponseWithBody) {
  NiceMock<MockDirectResponseEntry> direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::OK));
  const std::string response_body("static response");
  EXPECT_CALL(direct_response, responseBody()).WillRepeatedly(ReturnRef(response_body));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "15"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(1UL, config_.stats_.rq_direct_response_.value());
}

TEST_F(RouterTest, DirectResponseWithLocation) {
  NiceMock<MockDirectResponseEntry> direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, newPath(_)).WillOnce(Return("http://host/"));
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::Created));
  EXPECT_CALL(direct_response, responseBody()).WillRepeatedly(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "201"},
                                                   {"location", "http://host/"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(span_, injectContext(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(1UL, config_.stats_.rq_direct_response_.value());
}

TEST_F(RouterTest, DirectResponseWithoutLocation) {
  NiceMock<MockDirectResponseEntry> direct_response;
  std::string route_name("route-test-name");
  EXPECT_CALL(direct_response, newPath(_)).WillOnce(Return("http://host/"));
  EXPECT_CALL(direct_response, routeName()).WillOnce(ReturnRef(route_name));
  EXPECT_CALL(direct_response, responseCode()).WillRepeatedly(Return(Http::Code::OK));
  EXPECT_CALL(direct_response, responseBody()).WillRepeatedly(ReturnRef(EMPTY_STRING));
  EXPECT_CALL(*callbacks_.route_, directResponseEntry()).WillRepeatedly(Return(&direct_response));
  absl::string_view route_name_view(route_name);
  EXPECT_CALL(callbacks_.stream_info_, setRouteName(route_name_view));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(span_, injectContext(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(1UL, config_.stats_.rq_direct_response_.value());
}

// Allows verifying the state of the upstream StreamInfo
class TestAccessLog : public AccessLog::Instance {
public:
  explicit TestAccessLog(std::function<void(const StreamInfo::StreamInfo&)> func) : func_(func) {}

  void log(const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*,
           const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo& info) override {
    func_(info);
  }

private:
  std::function<void(const StreamInfo::StreamInfo&)> func_;
};

// Verifies that we propagate the upstream connection filter state to the upstream request filter
// state.
TEST_F(RouterTest, PropagatesUpstreamFilterState) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;

  // This pattern helps ensure that we're actually invoking the callback.
  bool filter_state_verified = false;
  router_.config().upstream_logs_.push_back(
      std::make_shared<TestAccessLog>([&](const auto& stream_info) {
        filter_state_verified = stream_info.upstreamFilterState()->hasDataWithName("upstream data");
      }));

  upstream_stream_info_.filterState()->setData(
      "upstream data", std::make_unique<StreamInfo::UInt32AccessorImpl>(123),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
  expectResponseTimerCreate();
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  EXPECT_TRUE(filter_state_verified);
}

TEST_F(RouterTest, UpstreamSSLConnection) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;

  std::string session_id = "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, sessionId()).WillByDefault(ReturnRef(session_id));
  upstream_stream_info_.setDownstreamSslConnection(connection_info);

  expectResponseTimerCreate();
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  ASSERT_NE(nullptr, callbacks_.streamInfo().upstreamSslConnection());
  EXPECT_EQ(session_id, callbacks_.streamInfo().upstreamSslConnection()->sessionId());
}

// Verify that upstream timing information is set into the StreamInfo after the upstream
// request completes.
TEST_F(RouterTest, UpstreamTimingSingleRequest) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  StreamInfo::StreamInfoImpl stream_info(test_time_.timeSystem(), nullptr);
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  EXPECT_FALSE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamRxByteReceived().has_value());

  Http::TestRequestHeaderMapImpl headers{};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  test_time_.advanceTimeWait(std::chrono::milliseconds(32));
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  test_time_.advanceTimeWait(std::chrono::milliseconds(43));

  // Confirm we still have no upstream timing data. It won't be set until after the
  // stream has ended.
  EXPECT_FALSE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamRxByteReceived().has_value());

  response_decoder->decodeData(data, true);

  // Now these should be set.
  EXPECT_TRUE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_TRUE(stream_info.lastUpstreamRxByteReceived().has_value());

  // Timings should match our sleep() calls.
  EXPECT_EQ(stream_info.lastUpstreamRxByteReceived().value() -
                stream_info.firstUpstreamRxByteReceived().value(),
            std::chrono::milliseconds(43));
  EXPECT_EQ(stream_info.lastUpstreamTxByteSent().value() -
                stream_info.firstUpstreamTxByteSent().value(),
            std::chrono::milliseconds(32));
}

// Verify that upstream timing information is set into the StreamInfo when a
// retry occurs (and not before).
TEST_F(RouterTest, UpstreamTimingRetry) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  StreamInfo::StreamInfoImpl stream_info(test_time_, nullptr);
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));

  // Check that upstream timing is updated after the first request.
  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.retry_state_->expectHeadersRetry();

  test_time_.advanceTimeWait(std::chrono::milliseconds(32));
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  test_time_.advanceTimeWait(std::chrono::milliseconds(43));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  // Check that upstream timing is not set when a retry will occur.
  Http::ResponseHeaderMapPtr bad_response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "503"}});
  response_decoder->decodeHeaders(std::move(bad_response_headers), true);
  EXPECT_FALSE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_FALSE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamRxByteReceived().has_value());

  router_.retry_state_->callback_();
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
  MonotonicTime retry_time = test_time_.monotonicTime();

  Http::ResponseHeaderMapPtr good_response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(good_response_headers), false);

  test_time_.advanceTimeWait(std::chrono::milliseconds(153));

  response_decoder->decodeData(data, true);

  EXPECT_TRUE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_TRUE(stream_info.lastUpstreamRxByteReceived().has_value());

  EXPECT_EQ(stream_info.lastUpstreamRxByteReceived().value() -
                stream_info.firstUpstreamRxByteReceived().value(),
            std::chrono::milliseconds(153));

  // Time spent in upstream tx is 0 because we're using simulated time and
  // don't have a good way to insert a "sleep" there, but values being present
  // and equal to the time the retry was sent is good enough of a test.
  EXPECT_EQ(stream_info.lastUpstreamTxByteSent().value() -
                stream_info.firstUpstreamTxByteSent().value(),
            std::chrono::milliseconds(0));
  EXPECT_EQ(stream_info.lastUpstreamTxByteSent().value() +
                stream_info.startTimeMonotonic().time_since_epoch(),
            retry_time.time_since_epoch());
  EXPECT_EQ(stream_info.firstUpstreamTxByteSent().value() +
                stream_info.startTimeMonotonic().time_since_epoch(),
            retry_time.time_since_epoch());
}

// Verify that upstream timing information is set into the StreamInfo when a
// global timeout occurs.
TEST_F(RouterTest, UpstreamTimingTimeout) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  StreamInfo::StreamInfoImpl stream_info(test_time_, nullptr);
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));

  expectResponseTimerCreate();
  test_time_.advanceTimeWait(std::chrono::milliseconds(10));

  // Check that upstream timing is updated after the first request.
  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "50"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  EXPECT_FALSE(stream_info.lastUpstreamRxByteReceived().has_value());

  test_time_.advanceTimeWait(std::chrono::milliseconds(13));
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  test_time_.advanceTimeWait(std::chrono::milliseconds(33));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);

  test_time_.advanceTimeWait(std::chrono::milliseconds(99));
  response_timeout_->invokeCallback();

  EXPECT_TRUE(stream_info.firstUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.lastUpstreamTxByteSent().has_value());
  EXPECT_TRUE(stream_info.firstUpstreamRxByteReceived().has_value());
  EXPECT_FALSE(stream_info.lastUpstreamRxByteReceived()
                   .has_value()); // False because no end_stream was seen.
  EXPECT_EQ(stream_info.firstUpstreamTxByteSent().value(), std::chrono::milliseconds(10));
  EXPECT_EQ(stream_info.lastUpstreamTxByteSent().value(), std::chrono::milliseconds(23));
  EXPECT_EQ(stream_info.firstUpstreamRxByteReceived().value(), std::chrono::milliseconds(56));
}

TEST(RouterFilterUtilityTest, FinalHedgingParamsHedgeOnPerTryTimeout) {
  Http::TestRequestHeaderMapImpl empty_headers;
  { // route says true, header not present, expect true.
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = true;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams =
        FilterUtility::finalHedgingParams(route, empty_headers);
    EXPECT_TRUE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says false, header not present, expect false.
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = false;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams =
        FilterUtility::finalHedgingParams(route, empty_headers);
    EXPECT_FALSE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says false, header says true, expect true.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "true"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = false;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_TRUE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says false, header says false, expect false.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "false"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = false;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_FALSE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says true, header says false, expect false.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "false"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = true;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_FALSE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says true, header says true, expect true.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "true"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = true;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_TRUE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says true, header is invalid, expect true.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "bad"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = true;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_TRUE(hedgingParams.hedge_on_per_try_timeout_);
  }
  { // route says false, header is invalid, expect false.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-hedge-on-per-try-timeout", "bad"}};
    NiceMock<MockRouteEntry> route;
    route.hedge_policy_.hedge_on_per_try_timeout_ = false;
    EXPECT_CALL(route, hedgePolicy).WillRepeatedly(ReturnRef(route.hedge_policy_));
    FilterUtility::HedgingParams hedgingParams = FilterUtility::finalHedgingParams(route, headers);
    EXPECT_FALSE(hedgingParams.hedge_on_per_try_timeout_);
  }
}

TEST(RouterFilterUtilityTest, FinalTimeout) {
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers;
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "bad"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("5", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, true, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, true, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("15m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    route.retry_policy_.per_try_timeout_ = std::chrono::milliseconds(7);
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(7), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("7", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    route.retry_policy_.per_try_timeout_ = std::chrono::milliseconds(10);
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(0)));
    Http::TestRequestHeaderMapImpl headers;
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    route.retry_policy_.per_try_timeout_ = std::chrono::milliseconds(7);
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("5", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout()).WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(1000), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_EQ("1000m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(999)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(999), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_EQ("999m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(999)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "0m"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(999), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_EQ("999m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(999)));
    EXPECT_CALL(route, grpcTimeoutOffset())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(10)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "100m"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(90), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(999)));
    EXPECT_CALL(route, grpcTimeoutOffset())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(10)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1m"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(1), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("15m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "bad"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(1000), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("1000", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("1000m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("15", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("15m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("5", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("5m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    route.retry_policy_.per_try_timeout_ = std::chrono::milliseconds(7);
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(7), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("7", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("7m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, maxGrpcTimeout())
        .WillRepeatedly(Return(absl::optional<std::chrono::milliseconds>(0)));
    route.retry_policy_.per_try_timeout_ = std::chrono::milliseconds(7);
    Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                           {"grpc-timeout", "1000m"},
                                           {"x-envoy-upstream-rq-timeout-ms", "15"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, true, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("5", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_EQ("5m", headers.get_("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-expected-rq-timeout-ms", "8"}};
    // Make ingress envoy respect `x-envoy-expected-rq-timeout-ms` header.
    bool respect_expected_rq_timeout = true;
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(
        route, headers, true, false, false, respect_expected_rq_timeout);
    EXPECT_EQ(std::chrono::milliseconds(8), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_EQ("8", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-expected-rq-timeout-ms", "8"},
                                           {"x-envoy-upstream-rq-per-try-timeout-ms", "4"}};
    // Make ingress envoy respect `x-envoy-expected-rq-timeout-ms` header.
    bool respect_expected_rq_timeout = true;
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(
        route, headers, true, false, false, respect_expected_rq_timeout);
    EXPECT_EQ(std::chrono::milliseconds(8), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(4), timeout.per_try_timeout_);
    EXPECT_EQ("4", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "8"}};
    // Test that ingress envoy populates `x-envoy-expected-rq-timeout-ms` header if it has not been
    // set by egress envoy.
    bool respect_expected_rq_timeout = true;
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(
        route, headers, true, false, false, respect_expected_rq_timeout);
    EXPECT_EQ(std::chrono::milliseconds(8), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("8", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "8"}};
    // Make envoy override `x-envoy-expected-rq-timeout-ms` header.
    // Test that ingress envoy sets `x-envoy-expected-rq-timeout-ms` header.
    bool respect_expected_rq_timeout = false;
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(
        route, headers, true, false, false, respect_expected_rq_timeout);
    EXPECT_EQ(std::chrono::milliseconds(8), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("8", headers.get_("x-envoy-expected-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("grpc-timeout"));
  }
}

TEST(RouterFilterUtilityTest, FinalTimeoutSupressEnvoyHeaders) {
  {
    NiceMock<MockRouteEntry> route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout =
        FilterUtility::finalTimeout(route, headers, true, false, false, false);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
  }
}

TEST(RouterFilterUtilityTest, SetUpstreamScheme) {
  {
    Http::TestRequestHeaderMapImpl headers;
    FilterUtility::setUpstreamScheme(headers, false);
    EXPECT_EQ("http", headers.get_(":scheme"));
  }
  {
    Http::TestRequestHeaderMapImpl headers;
    FilterUtility::setUpstreamScheme(headers, true);
    EXPECT_EQ("https", headers.get_(":scheme"));
  }
}

TEST(RouterFilterUtilityTest, ShouldShadow) {
  {
    TestShadowPolicy policy;
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled(_, _, _, _)).Times(0);
    EXPECT_FALSE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy("cluster");
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled(_, _, _, _)).Times(0);
    EXPECT_TRUE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy("cluster", "foo");
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("foo", 0, 5, 10000)).WillOnce(Return(false));
    EXPECT_FALSE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy("cluster", "foo");
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("foo", 0, 5, 10000)).WillOnce(Return(true));
    EXPECT_TRUE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  // Use default value instead of runtime key.
  {
    envoy::type::v3::FractionalPercent fractional_percent;
    fractional_percent.set_numerator(5);
    fractional_percent.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
    TestShadowPolicy policy("cluster", "foo", fractional_percent);
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(
        runtime.snapshot_,
        featureEnabled("foo", testing::Matcher<const envoy::type::v3::FractionalPercent&>(_), 3))
        .WillOnce(Return(true));
    EXPECT_TRUE(FilterUtility::shouldShadow(policy, runtime, 3));
  }
}

TEST_F(RouterTest, CanaryStatusTrue) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-alt-stat-name", "alt_stat"},
                                         {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"},
                                          {"x-envoy-upstream-canary", "false"},
                                          {"x-envoy-virtual-cluster", "hello"}});
  ON_CALL(*cm_.thread_local_cluster_.conn_pool_.host_, canary()).WillByDefault(Return(true));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("canary.upstream_rq_200")
                .value());
}

TEST_F(RouterTest, CanaryStatusFalse) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-upstream-alt-stat-name", "alt_stat"},
                                         {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"},
                                          {"x-envoy-upstream-canary", "false"},
                                          {"x-envoy-virtual-cluster", "hello"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  EXPECT_EQ(0U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("canary.upstream_rq_200")
                .value());
}

TEST_F(RouterTest, AutoHostRewriteEnabled) {
  NiceMock<Http::MockRequestEncoder> encoder;
  std::string req_host{"foo.bar.com"};

  Http::TestRequestHeaderMapImpl incoming_headers;
  HttpTestUtility::addDefaultHeaders(incoming_headers);
  incoming_headers.setHost(req_host);

  cm_.thread_local_cluster_.conn_pool_.host_->hostname_ = "scooby.doo";
  Http::TestRequestHeaderMapImpl outgoing_headers;
  HttpTestUtility::addDefaultHeaders(outgoing_headers);
  outgoing_headers.setHost(cm_.thread_local_cluster_.conn_pool_.host_->hostname_);

  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, Http::Protocol::Http10);
        return nullptr;
      }));

  // :authority header in the outgoing request should match the DNS name of
  // the selected upstream host
  EXPECT_CALL(encoder, encodeHeaders(HeaderMapEqualRef(&outgoing_headers), true))
      .WillOnce(Invoke([&](const Http::HeaderMap&, bool) -> Http::Status {
        encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
        return Http::okStatus();
      }));

  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));
  EXPECT_CALL(callbacks_.route_->route_entry_, autoHostRewrite()).WillOnce(Return(true));
  router_.decodeHeaders(incoming_headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, AutoHostRewriteDisabled) {
  NiceMock<Http::MockRequestEncoder> encoder;
  std::string req_host{"foo.bar.com"};

  Http::TestRequestHeaderMapImpl incoming_headers;
  HttpTestUtility::addDefaultHeaders(incoming_headers);
  incoming_headers.setHost(req_host);

  cm_.thread_local_cluster_.conn_pool_.host_->hostname_ = "scooby.doo";

  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
        callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, Http::Protocol::Http10);
        return nullptr;
      }));

  // :authority header in the outgoing request should match the :authority header of
  // the incoming request
  EXPECT_CALL(encoder, encodeHeaders(HeaderMapEqualRef(&incoming_headers), true))
      .WillOnce(Invoke([&](const Http::HeaderMap&, bool) -> Http::Status {
        encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
        return Http::okStatus();
      }));

  EXPECT_CALL(callbacks_.stream_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionConstSharedPtr host) -> void {
        EXPECT_EQ(host_address_, host->address());
      }));
  EXPECT_CALL(callbacks_.route_->route_entry_, autoHostRewrite()).WillOnce(Return(false));
  router_.decodeHeaders(incoming_headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

TEST_F(RouterTest, UpstreamSocketOptionsReturnedEmpty) {
  EXPECT_CALL(callbacks_, getUpstreamSocketOptions())
      .WillOnce(Return(Network::Socket::OptionsSharedPtr()));

  auto options = router_.upstreamSocketOptions();

  EXPECT_EQ(options.get(), nullptr);
}

TEST_F(RouterTest, UpstreamSocketOptionsReturnedNonEmpty) {
  Network::Socket::OptionsSharedPtr to_return =
      Network::SocketOptionFactory::buildIpTransparentOptions();
  EXPECT_CALL(callbacks_, getUpstreamSocketOptions()).WillOnce(Return(to_return));

  auto options = router_.upstreamSocketOptions();

  EXPECT_EQ(to_return, options);
}

TEST_F(RouterTest, ApplicationProtocols) {
  callbacks_.streamInfo().filterState()->setData(
      Network::ApplicationProtocols::key(),
      std::make_unique<Network::ApplicationProtocols>(std::vector<std::string>{"foo", "bar"}),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                     Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
            Network::TransportSocketOptionsSharedPtr transport_socket_options =
                context->upstreamTransportSocketOptions();
            EXPECT_NE(transport_socket_options, nullptr);
            EXPECT_FALSE(transport_socket_options->applicationProtocolListOverride().empty());
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride().size(), 2);
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride()[0], "foo");
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride()[1], "bar");
            return &cm_.thread_local_cluster_.conn_pool_;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Return(&cancellable_));

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(span_, injectContext(_));
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

// Verify that CONNECT payload is not sent upstream until :200 response headers
// are received.
TEST_F(RouterTest, ConnectPauseAndResume) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  EXPECT_CALL(encoder, encodeHeaders(_, false));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("CONNECT");
  router_.decodeHeaders(headers, false);

  // Make sure any early data does not go upstream.
  EXPECT_CALL(encoder, encodeData(_, _)).Times(0);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);

  // Now send the response headers, and ensure the deferred payload is proxied.
  EXPECT_CALL(encoder, encodeData(_, _));
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

// Invalid upstream will fail over to generic in opt mode, but crash in debug mode.
TEST_F(RouterTest, InvalidUpstream) {
  // Explicitly configure an HTTP upstream, to test factory creation.
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_ =
      absl::make_optional<envoy::config::core::v3::TypedExtensionConfig>();
  // Configure a TCP upstream rather than an HTTP upstream.
  envoy::extensions::upstreams::tcp::generic::v3::GenericConnectionPoolProto generic_config;
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_.value()
      .mutable_typed_config()
      ->PackFrom(generic_config);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  ON_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillByDefault(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("CONNECT");
  EXPECT_DEBUG_DEATH(router_.decodeHeaders(headers, false),
                     "envoy bug failure: factory != nullptr.");

  router_.onDestroy();
}

// Verify that CONNECT payload is not sent upstream if non-200 response headers are received.
TEST_F(RouterTest, ConnectPauseNoResume) {
  // Explicitly configure an HTTP upstream, to test factory creation.
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_ =
      absl::make_optional<envoy::config::core::v3::TypedExtensionConfig>();
  envoy::extensions::upstreams::http::http::v3::HttpConnectionPoolProto http_config;
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_.value()
      .mutable_typed_config()
      ->PackFrom(http_config);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke(
          [&](Http::ResponseDecoder& decoder,
              Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  EXPECT_CALL(encoder, encodeHeaders(_, false));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("CONNECT");
  router_.decodeHeaders(headers, false);

  // Make sure any early data does not go upstream.
  EXPECT_CALL(encoder, encodeData(_, _)).Times(0);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);

  // Now send the response headers, and ensure the deferred payload is not proxied.
  EXPECT_CALL(encoder, encodeData(_, _)).Times(0);
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "400"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

TEST_F(RouterTest, ConnectExplicitTcpUpstream) {
  // Explicitly configure an TCP upstream, to test factory creation.
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_ =
      absl::make_optional<envoy::config::core::v3::TypedExtensionConfig>();
  envoy::extensions::upstreams::http::tcp::v3::TcpConnectionPoolProto tcp_config;
  cm_.thread_local_cluster_.cluster_.info_->upstream_config_.value()
      .mutable_typed_config()
      ->PackFrom(tcp_config);
  callbacks_.route_->route_entry_.connect_config_ =
      absl::make_optional<RouteEntry::ConnectConfig>();

  // Make sure newConnection is called on the TCP pool, not newStream on the HTTP pool.
  EXPECT_CALL(cm_.thread_local_cluster_.tcp_conn_pool_, newConnection(_));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("CONNECT");
  router_.decodeHeaders(headers, false);

  router_.onDestroy();
}

} // namespace Router
} // namespace Envoy
