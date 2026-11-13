#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.pb.h"
#include "envoy/extensions/formatter/req_without_query/v3/req_without_query.pb.h"
#include "envoy/extensions/http/stateful_session/header/v3/header.pb.h"
#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"
#include "envoy/extensions/path/rewrite/uri_template/v3/uri_template_rewrite.pb.h"
#include "envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/deferred_virtual_host.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

void packAny(Protobuf::Any& any, const Protobuf::Message& message) {
  any.set_type_url(TypeUtil::descriptorFullNameToTypeUrl(message.GetTypeName()));
  ASSERT_TRUE(message.SerializeToString(any.mutable_value()));
}

Http::TestRequestHeaderMapImpl genHeaders(absl::string_view host, absl::string_view path,
                                          absl::string_view method = "GET",
                                          absl::string_view scheme = "http") {
  return Http::TestRequestHeaderMapImpl{{":authority", std::string(host)},
                                        {":path", std::string(path)},
                                        {":method", std::string(method)},
                                        {":scheme", std::string(scheme)},
                                        {"x-forwarded-proto", std::string(scheme)}};
}

#if defined(ENVOY_ENABLE_YAML)
envoy::config::route::v3::RouteConfiguration parseRouteConfig(const std::string& yaml) {
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  return route_config;
}

SINGLETON_MANAGER_REGISTRATION(deferred_vhost_test_singleton);
SINGLETON_MANAGER_REGISTRATION(deferred_vhost_parent_singleton);
#endif

class DeferredVirtualHostTest : public testing::Test {
protected:
  DeferredVirtualHostTest() : api_(Api::createApiForTest()) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
    factory_context_.bootstrap_.mutable_route_manager()->set_enable_deferred_virtual_host_creation(
        true);
  }

  std::shared_ptr<ConfigImpl> createConfig(const envoy::config::route::v3::RouteConfiguration& rc,
                                           bool validate_clusters = true) {
    auto config_or_error =
        ConfigImpl::create(rc, factory_context_, ProtobufMessage::getNullValidationVisitor(),
                           init_manager_, validate_clusters);
    EXPECT_OK(config_or_error.status());
    return config_or_error.ok() ? std::move(config_or_error.value()) : nullptr;
  }

  std::unique_ptr<RouteMatcher>
  createMatcher(const envoy::config::route::v3::RouteConfiguration& rc,
                bool validate_clusters = true) {
    auto global_or_error = CommonConfigImpl::create(
        rc, factory_context_, ProtobufMessage::getNullValidationVisitor(), init_manager_);
    EXPECT_OK(global_or_error.status());
    if (!global_or_error.ok()) {
      return nullptr;
    }
    auto matcher_or_error = RouteMatcher::create(rc, global_or_error.value(), factory_context_,
                                                 ProtobufMessage::getNullValidationVisitor(),
                                                 init_manager_, validate_clusters);
    EXPECT_OK(matcher_or_error.status());
    return matcher_or_error.ok() ? std::move(matcher_or_error.value()) : nullptr;
  }

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Init::MockManager> init_manager_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(DeferredVirtualHostTest, BasicProtoRoutingAndLiteFallback) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1"}, {});
  envoy::config::route::v3::RouteConfiguration rc;
  auto* vhost = rc.add_virtual_hosts();
  vhost->set_name("proto_vhost");
  vhost->add_domains("proto.example.com");
  auto* route = vhost->add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_route()->set_cluster("cluster_1");

  auto global_or_error = CommonConfigImpl::create(
      rc, factory_context_, ProtobufMessage::getNullValidationVisitor(), init_manager_);
  ASSERT_OK(global_or_error.status());
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_FALSE(requiresMainThreadInit(*vhost));
  EXPECT_FALSE(requiresProbeValidation(*vhost, global_or_error.value(), true, factory_context_));
#else
  EXPECT_TRUE(requiresMainThreadInit(*vhost));
  EXPECT_TRUE(requiresProbeValidation(*vhost, global_or_error.value(), true, factory_context_));
#endif

  auto matcher = createMatcher(rc);
  ASSERT_NE(matcher, nullptr);
  const DomainEntry* entry = matcher->findDomainEntry("proto.example.com");
  ASSERT_NE(entry, nullptr);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_TRUE(entry->isDeferred());
#else
  EXPECT_FALSE(entry->isDeferred());
#endif

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  const auto matched = config->route(genHeaders("proto.example.com", "/"), stream_info_, 0);
  ASSERT_NE(matched.route, nullptr);
  EXPECT_EQ("proto_vhost", matched.route->virtualHost().name());

  envoy::config::route::v3::VirtualHost vhost_with_any = *vhost;
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute buf;
  buf.set_disabled(true);
  envoy::config::route::v3::FilterConfig fc;
  fc.set_disabled(true);
  packAny(*fc.mutable_config(), buf);
  packAny((*vhost_with_any.mutable_typed_per_filter_config())["buf"], fc);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_FALSE(requiresMainThreadInit(vhost_with_any));
#else
  EXPECT_TRUE(requiresMainThreadInit(vhost_with_any));
#endif
}

#if defined(ENVOY_ENABLE_YAML)
TEST_F(DeferredVirtualHostTest, DisabledAndEnabledRoutingAndWildcardPrecedence) {
  const std::string yaml = R"EOF(
ignore_port_in_host_matching: true
virtual_hosts:
- name: vhost_default
  domains: ["*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_default" }
- name: vhost_short_prefix
  domains: ["service.*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_short_prefix" }
- name: vhost_long_prefix
  domains: ["service.sub.*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_long_prefix" }
- name: vhost_short_suffix
  domains: ["*.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_short_suffix" }
- name: vhost_long_suffix
  domains: ["*.sub.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_long_suffix" }
- name: vhost_exact
  domains: ["service.sub.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_exact" }
)EOF";

  factory_context_.cluster_manager_.initializeClusters(
      {"cluster_exact", "cluster_long_suffix", "cluster_short_suffix", "cluster_long_prefix",
       "cluster_short_prefix", "cluster_default"},
      {});
  const auto rc = parseRouteConfig(yaml);

  // 1. When disabled, all virtual hosts are eagerly instantiated at config creation time.
  factory_context_.bootstrap_.mutable_route_manager()->set_enable_deferred_virtual_host_creation(
      false);
  {
    auto matcher = createMatcher(rc);
    ASSERT_NE(matcher, nullptr);
    const DomainEntry* exact_entry = matcher->findDomainEntry("service.sub.example.com");
    ASSERT_NE(exact_entry, nullptr);
    EXPECT_FALSE(exact_entry->isDeferred());
    EXPECT_NE(exact_entry->activeVirtualHost(), nullptr);
  }

  // 2. When enabled, virtual hosts remain dormant until matched; virtualHostExists() does NOT
  // inflate dormant virtual hosts.
  factory_context_.bootstrap_.mutable_route_manager()->set_enable_deferred_virtual_host_creation(
      true);
  auto matcher = createMatcher(rc);
  ASSERT_NE(matcher, nullptr);
  const DomainEntry* exact_entry = matcher->findDomainEntry("SERVICE.SUB.EXAMPLE.COM:8443");
  ASSERT_NE(exact_entry, nullptr);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_TRUE(exact_entry->isDeferred());
  EXPECT_EQ(exact_entry->activeVirtualHost(), nullptr);
#else
  EXPECT_FALSE(exact_entry->isDeferred());
  EXPECT_NE(exact_entry->activeVirtualHost(), nullptr);
#endif

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  EXPECT_TRUE(config->virtualHostExists("service.sub.example.com:8080"));
  EXPECT_TRUE(config->virtualHostExists("unmatched.domain"));

  // Verify precedence and caching across lookups.
  const auto route_exact_1 =
      config->route(genHeaders("SERVICE.SUB.EXAMPLE.COM:8443", "/"), stream_info_, 0);
  const auto route_exact_2 =
      config->route(genHeaders("service.sub.example.com", "/"), stream_info_, 0);
  ASSERT_NE(route_exact_1.route, nullptr);
  ASSERT_NE(route_exact_2.route, nullptr);
  EXPECT_EQ("vhost_exact", route_exact_1.route->virtualHost().name());
  EXPECT_EQ(&route_exact_1.route->virtualHost(), &route_exact_2.route->virtualHost());

  EXPECT_EQ("vhost_long_suffix",
            config->route(genHeaders("foo.sub.example.com", "/"), stream_info_, 0)
                .route->virtualHost()
                .name());
  EXPECT_EQ("vhost_short_suffix", config->route(genHeaders("foo.example.com", "/"), stream_info_, 0)
                                      .route->virtualHost()
                                      .name());
  EXPECT_EQ("vhost_long_prefix", config->route(genHeaders("service.sub.org", "/"), stream_info_, 0)
                                     .route->virtualHost()
                                     .name());
  EXPECT_EQ(
      "vhost_short_prefix",
      config->route(genHeaders("service.net", "/"), stream_info_, 0).route->virtualHost().name());
  EXPECT_EQ("vhost_default", config->route(genHeaders("unmatched.other.org", "/"), stream_info_, 0)
                                 .route->virtualHost()
                                 .name());
}

TEST_F(DeferredVirtualHostTest, VhostHeaderAndMissingAuthoritySemanticsPreserved) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1"}, {});

  const auto rc = parseRouteConfig(R"EOF(
vhost_header: "x-vhost"
virtual_hosts:
- name: vhost_1
  domains: ["alt.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
- name: vhost_2
  domains: ["host.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF");

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  EXPECT_FALSE(config->virtualHostExists("nonexistent.example.com"));
  EXPECT_TRUE(config->virtualHostExists("alt.example.com"));

  // Missing vhost_header must NOT fall back to :authority.
  auto missing_vhost_hdr = genHeaders("host.example.com", "/");
  EXPECT_EQ(config->route(missing_vhost_hdr, stream_info_, 0).route, nullptr);

  // Present vhost_header selects vhost_1.
  missing_vhost_hdr.addCopy("x-vhost", "alt.example.com");
  auto matched = config->route(missing_vhost_hdr, stream_info_, 0);
  ASSERT_NE(matched.route, nullptr);
  EXPECT_EQ("vhost_1", matched.route->virtualHost().name());

  // Without vhost_header configured, missing :authority returns nullptr when non-default domains
  // exist.
  const auto no_vhost_header_rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: vhost_only
  domains: ["only.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF");
  auto config_no_alt = createConfig(no_vhost_header_rc);
  ASSERT_NE(config_no_alt, nullptr);
  Http::TestRequestHeaderMapImpl no_authority{
      {":path", "/"}, {":method", "GET"}, {":scheme", "http"}};
  EXPECT_EQ(config_no_alt->route(no_authority, stream_info_, 0).route, nullptr);
}

TEST_F(DeferredVirtualHostTest, DomainEntrySharedOwnershipAndConcurrentInflation) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1"}, {});
  const auto rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: concurrent_vhost
  domains: ["concurrent.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF");

  // 1. Verify DomainEntry null safety and inflation error handling without uncaught exceptions.
  auto global_or_error = CommonConfigImpl::create(
      rc, factory_context_, ProtobufMessage::getNullValidationVisitor(), init_manager_);
  ASSERT_OK(global_or_error.status());
  auto vhost_scope = factory_context_.scope().scopeFromStatName(Stats::StatName());

  DomainEntry empty_entry;
  EXPECT_FALSE(empty_entry.isDeferred());
  EXPECT_EQ(empty_entry.activeVirtualHost(), nullptr);
  EXPECT_EQ(
      empty_entry.getOrCreateVirtualHost(global_or_error.value(), factory_context_, vhost_scope),
      nullptr);

  envoy::config::route::v3::VirtualHost invalid_vhost;
  invalid_vhost.set_name("invalid_vhost");
  invalid_vhost.add_domains("invalid.example.com");
  invalid_vhost.add_routes()->mutable_route()->set_cluster("cluster_1"); // Missing path specifier.
  DomainEntry bad_entry;
  bad_entry.initDeferred(&invalid_vhost);
  EXPECT_TRUE(bad_entry.isDeferred());
  EXPECT_EQ(bad_entry.activeVirtualHost(), nullptr);
  EXPECT_EQ(
      bad_entry.getOrCreateVirtualHost(global_or_error.value(), factory_context_, vhost_scope),
      nullptr);

  // 2. Verify returned VirtualHostImplConstSharedPtr keeps VirtualHostImpl alive after DomainEntry
  // and RouteMatcher are destroyed (eviction-ready lifetime invariant).
  VirtualHostImplConstSharedPtr retained_vhost;
  {
    auto matcher = createMatcher(rc);
    ASSERT_NE(matcher, nullptr);
    retained_vhost = matcher->findVirtualHost("concurrent.example.com");
    ASSERT_NE(retained_vhost, nullptr);
  }
  EXPECT_EQ("concurrent_vhost", retained_vhost->virtualHost()->name());
  retained_vhost.reset();

  // 3. Concurrent inflation across 8 worker threads.
  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  constexpr int kNumThreads = 8;
  constexpr int kIterations = 100;
  std::atomic<bool> start_gate{false};
  std::atomic<int> success_count{0};
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      while (!start_gate.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
      for (int j = 0; j < kIterations; ++j) {
        auto headers = genHeaders("concurrent.example.com", "/test");
        const auto route = config->route(headers, local_info, 0);
        if (route.route != nullptr && route.route->routeEntry()->clusterName() == "cluster_1") {
          success_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  start_gate.store(true, std::memory_order_release);
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(kNumThreads * kIterations, success_count.load());
}

TEST_F(DeferredVirtualHostTest, ShallowRequiresMainThreadInitChecks) {
  const auto simple_rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: simple_vhost
  domains: ["simple.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF");
  const auto& base = simple_rc.virtual_hosts(0);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_FALSE(requiresMainThreadInit(base));
#else
  EXPECT_TRUE(requiresMainThreadInit(base));
#endif

  // 1. route_specifiers on VirtualHost, Route, and matcher Route / RouteList.
  {
    auto v = base;
    v.add_route_specifiers()->set_name("specifier");
    EXPECT_TRUE(requiresMainThreadInit(v));
  }
  {
    auto v = base;
    v.mutable_routes(0)->add_route_specifiers()->set_name("specifier");
    EXPECT_TRUE(requiresMainThreadInit(v));
  }

  // 2. Watched vs static direct_response.body: watched_directory short-circuits to eager
  // main-thread init without opening throwaway probe watchers, whereas static inline_string /
  // filename stays deferred without touching mainThreadDispatcher() or threadLocal().
  {
    auto* watcher = new Filesystem::MockWatcher();
    EXPECT_CALL(factory_context_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
    EXPECT_CALL(*watcher, addWatch(_, _, _)).WillOnce(Return(absl::OkStatus()));

    const std::string file_path =
        TestEnvironment::writeStringToFileForTest("dynamic_body.txt", "dynamic content");
    const auto watched_rc =
        parseRouteConfig(fmt::format(R"EOF(
virtual_hosts:
- name: dynamic_direct_response_vhost
  domains: ["dynamic.example.com"]
  routes:
  - match: {{ prefix: "/" }}
    direct_response:
      status: 200
      body:
        filename: "{}"
        watched_directory:
          path: "{}"
)EOF",
                                     file_path, TestEnvironment::temporaryDirectory()));
    EXPECT_TRUE(requiresMainThreadInit(watched_rc.virtual_hosts(0)));
    EXPECT_TRUE(
        requiresProbeValidation(watched_rc.virtual_hosts(0), nullptr, false, factory_context_));

    auto matcher = createMatcher(watched_rc, false);
    ASSERT_NE(matcher, nullptr);
    const DomainEntry* entry = matcher->findDomainEntry("dynamic.example.com");
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->isDeferred());

    // Static direct_response.body (inline_string and static file) stays deferred and formats
    // properly on worker threads.
    EXPECT_CALL(factory_context_, mainThreadDispatcher()).Times(0);
    EXPECT_CALL(factory_context_, threadLocal()).Times(0);
    const auto static_rc = parseRouteConfig(fmt::format(R"EOF(
virtual_hosts:
- name: static_direct_response_vhost
  domains: ["static.example.com"]
  routes:
  - match: {{ path: "/inline" }}
    direct_response:
      status: 200
      body:
        inline_string: "inline payload"
  - match: {{ prefix: "/" }}
    direct_response:
      status: 200
      body:
        filename: "{}"
)EOF",
                                                        file_path));
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(static_rc.virtual_hosts(0)));
#endif
    auto static_matcher = createMatcher(static_rc, false);
    ASSERT_NE(static_matcher, nullptr);
    const DomainEntry* static_entry = static_matcher->findDomainEntry("static.example.com");
    ASSERT_NE(static_entry, nullptr);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_TRUE(static_entry->isDeferred());
#endif
    auto static_config = createConfig(static_rc, false);
    ASSERT_NE(static_config, nullptr);
    std::string body_out;
    Http::TestResponseHeaderMapImpl resp_headers;
    const auto inline_route =
        static_config->route(genHeaders("static.example.com", "/inline"), stream_info_, 0).route;
    ASSERT_NE(inline_route, nullptr);
    ASSERT_NE(inline_route->directResponseEntry(), nullptr);
    EXPECT_EQ("inline payload", inline_route->directResponseEntry()->formatBody(
                                    genHeaders("static.example.com", "/inline"), resp_headers,
                                    stream_info_, body_out));
    const auto file_route =
        static_config->route(genHeaders("static.example.com", "/other"), stream_info_, 0).route;
    ASSERT_NE(file_route, nullptr);
    ASSERT_NE(file_route->directResponseEntry(), nullptr);
    EXPECT_EQ("dynamic content", file_route->directResponseEntry()->formatBody(
                                     genHeaders("static.example.com", "/other"), resp_headers,
                                     stream_info_, body_out));
    testing::Mock::VerifyAndClearExpectations(&factory_context_);
  }

  // 3. Per-filter configs: unvetted Any types, FilterConfig wrapper (disabled, empty re-enable,
  // vetted BufferPerRoute, unvetted dynamic_modules), TypedStruct / Struct conservative fallback,
  // OnDemandCds, and per-route LocalRateLimit.
  const std::string dm_type_url =
      "type.googleapis.com/"
      "envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilterPerRoute";
  {
    auto v = base;
    (*v.mutable_typed_per_filter_config())["dm"].set_type_url(dm_type_url);
    EXPECT_TRUE(requiresMainThreadInit(v));
  }
  {
    // Disabled FilterConfig wrapping dynamic_modules does NOT require main-thread init.
    auto v = base;
    envoy::config::route::v3::FilterConfig fc;
    fc.set_disabled(true);
    fc.mutable_config()->set_type_url(dm_type_url);
    packAny((*v.mutable_typed_per_filter_config())["dm"], fc);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v));
#endif

    // FilterConfig with empty config Any (explicit filter re-enable) does NOT require main-thread
    // init.
    fc.set_disabled(false);
    fc.mutable_config()->Clear();
    packAny((*v.mutable_typed_per_filter_config())["dm"], fc);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v));
#endif

    // Enabled FilterConfig wrapping vetted BufferPerRoute does NOT require main-thread init.
    envoy::extensions::filters::http::buffer::v3::BufferPerRoute buffer_per_route;
    buffer_per_route.mutable_buffer()->mutable_max_request_bytes()->set_value(4096);
    packAny(*fc.mutable_config(), buffer_per_route);
    packAny((*v.mutable_typed_per_filter_config())["buf"], fc);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v));
#endif

    // Enabled FilterConfig wrapping dynamic_modules DOES require main-thread init.
    fc.mutable_config()->set_type_url(dm_type_url);
    packAny((*v.mutable_typed_per_filter_config())["dm"], fc);
    EXPECT_TRUE(requiresMainThreadInit(v));
  }
  {
    // xds.type.v3.TypedStruct and udpa.type.v1.TypedStruct conservatively require main-thread init
    // regardless of inner type or disabled flag.
    auto v = base;
    xds::type::v3::TypedStruct ts;
    ts.set_type_url("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute");
    packAny((*v.mutable_routes(0)->mutable_typed_per_filter_config())["buf"], ts);
    EXPECT_TRUE(requiresMainThreadInit(v));

    udpa::type::v1::TypedStruct legacy_fc_ts;
    legacy_fc_ts.set_type_url("type.googleapis.com/envoy.config.route.v3.FilterConfig");
    (*legacy_fc_ts.mutable_value()->mutable_fields())["disabled"].set_bool_value(true);
    auto v2 = base;
    auto* wc = v2.mutable_routes(0)->mutable_route()->mutable_weighted_clusters()->add_clusters();
    wc->set_name("cluster_1");
    packAny((*wc->mutable_typed_per_filter_config())["dm"], legacy_fc_ts);
    EXPECT_TRUE(requiresMainThreadInit(v2));
  }
  {
    // Per-route LocalRateLimit and OnDemandCds short-circuit to eager main-thread init without
    // creating throwaway probe timers or ODCDS subscriptions.
    envoy::extensions::filters::http::on_demand::v3::PerRouteConfig od_per_route;
    od_per_route.mutable_odcds()->mutable_source()->set_path("/tmp/odcds.yaml");
    auto v_od = base;
    packAny((*v_od.mutable_routes(0)
                  ->mutable_typed_per_filter_config())["envoy.filters.http.on_demand"],
            od_per_route);
    EXPECT_TRUE(requiresMainThreadInit(v_od));

    envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit local_rl;
    local_rl.set_stat_prefix("local_rl");
    local_rl.mutable_token_bucket()->set_max_tokens(10);
    auto v_local_rl = base;
    packAny((*v_local_rl.mutable_routes(0)
                  ->mutable_typed_per_filter_config())["envoy.filters.http.local_ratelimit"],
            local_rl);
    EXPECT_TRUE(requiresMainThreadInit(v_local_rl));

    // StatefulSessionPerRoute with HeaderBasedSessionState stays deferred, whereas
    // CookieBasedSessionState (which accesses mainThreadDispatcher().timeSource()) is eager.
    envoy::extensions::http::stateful_session::header::v3::HeaderBasedSessionState hdr_session;
    hdr_session.set_name("x-session");
    envoy::extensions::filters::http::stateful_session::v3::StatefulSessionPerRoute ss_per_route;
    auto* session_ext = ss_per_route.mutable_stateful_session()->mutable_session_state();
    session_ext->set_name("envoy.http.stateful_session.header");
    packAny(*session_ext->mutable_typed_config(), hdr_session);
    auto v_ss = base;
    packAny((*v_ss.mutable_routes(0)->mutable_typed_per_filter_config())["ss"], ss_per_route);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_ss));
#endif

    session_ext->mutable_typed_config()->set_type_url(
        "type.googleapis.com/"
        "envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState");
    packAny((*v_ss.mutable_routes(0)->mutable_typed_per_filter_config())["ss"], ss_per_route);
    EXPECT_TRUE(requiresMainThreadInit(v_ss));
  }

  // 4. Allowlist keys on Any.type_url rather than TypedExtensionConfig.name user labels, and
  // catches previously bypassed extension points (generic_secret, StringMatcher.custom,
  // uri_template input matcher vs path match/rewrite, rate_limits[].actions[].extension,
  // RetryHostPredicate/RetryPriority without typed_config, and nested filter formatters).
  {
    // User label containing "dynamic_modules" paired with vetted ReqWithoutQuery formatter or
    // HttpRequestHeaderMatchInput -> deferred!
    envoy::extensions::formatter::req_without_query::v3::ReqWithoutQuery req_wo_query;
    auto v_vetted_fmt = base;
    v_vetted_fmt.mutable_routes(0)->clear_route();
    v_vetted_fmt.mutable_routes(0)->mutable_direct_response()->set_status(200);
    auto* fmt_ext = v_vetted_fmt.mutable_routes(0)
                        ->mutable_direct_response()
                        ->mutable_body_format()
                        ->add_formatters();
    fmt_ext->set_name("user_label_with_dynamic_modules_in_name");
    packAny(*fmt_ext->mutable_typed_config(), req_wo_query);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_vetted_fmt));

    envoy::type::matcher::v3::HttpRequestHeaderMatchInput dm_named_hdr_input;
    dm_named_hdr_input.set_header_name("x-check");
    envoy::config::route::v3::RouteConfiguration dm_label_rc;
    auto* mv = dm_label_rc.add_virtual_hosts();
    mv->set_name("dm_label_vhost");
    mv->add_domains("dmlabel.example.com");
    auto* fm = mv->mutable_matcher()->mutable_matcher_list()->add_matchers();
    auto* sp = fm->mutable_predicate()->mutable_single_predicate();
    sp->mutable_input()->set_name("user_label_with_dynamic_modules_and_wasm_and_lua");
    packAny(*sp->mutable_input()->mutable_typed_config(), dm_named_hdr_input);
    sp->mutable_value_match()->set_exact("1");
    packAny(*fm->mutable_on_match()->mutable_action()->mutable_typed_config(), base.routes(0));
    auto dm_label_matcher = createMatcher(dm_label_rc, false);
    ASSERT_NE(dm_label_matcher, nullptr);
    EXPECT_TRUE(dm_label_matcher->findDomainEntry("dmlabel.example.com")->isDeferred());
#endif

    // Harmless user label paired with generic_secret or file_content formatter -> eager!
    auto v_secret = base;
    v_secret.mutable_routes(0)->clear_route();
    v_secret.mutable_routes(0)->mutable_direct_response()->set_status(200);
    auto* secret_ext = v_secret.mutable_routes(0)
                           ->mutable_direct_response()
                           ->mutable_body_format()
                           ->add_formatters();
    secret_ext->set_name("harmless_label");
    secret_ext->mutable_typed_config()->set_type_url(
        "type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret");
    EXPECT_TRUE(requiresMainThreadInit(v_secret));

    auto v_file_content = base;
    v_file_content.mutable_routes(0)->clear_route();
    v_file_content.mutable_routes(0)->mutable_direct_response()->set_status(200);
    v_file_content.mutable_routes(0)
        ->mutable_direct_response()
        ->mutable_body_format()
        ->add_formatters()
        ->mutable_typed_config()
        ->set_type_url(
            "type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent");
    EXPECT_TRUE(requiresMainThreadInit(v_file_content));

    // TypedExtensionConfig, RetryHostPredicate, and RetryPriority missing typed_config ->
    // conservatively eager; PreviousHostsPredicate with typed_config -> deferred!
    auto v_missing_any = base;
    v_missing_any.mutable_routes(0)
        ->mutable_route()
        ->mutable_inline_cluster_specifier_plugin()
        ->mutable_extension()
        ->set_name("missing_typed_config");
    EXPECT_TRUE(requiresMainThreadInit(v_missing_any));

    auto v_missing_retry_pred = base;
    v_missing_retry_pred.mutable_retry_policy()->add_retry_host_predicate()->set_name(
        "unvetted.retry.host_predicate");
    EXPECT_TRUE(requiresMainThreadInit(v_missing_retry_pred));

    auto v_missing_retry_prio = base;
    v_missing_retry_prio.mutable_routes(0)
        ->mutable_route()
        ->mutable_retry_policy()
        ->mutable_retry_priority()
        ->set_name("unvetted.retry.priority");
    EXPECT_TRUE(requiresMainThreadInit(v_missing_retry_prio));

    envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate prev_hosts;
    auto v_prev_hosts = base;
    auto* rhp = v_prev_hosts.mutable_retry_policy()->add_retry_host_predicate();
    rhp->set_name("envoy.retry_host_predicates.previous_hosts");
    packAny(*rhp->mutable_typed_config(), prev_hosts);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_prev_hosts));
#endif

    // StringMatcher.custom (e.g., Lua string matcher on HeaderMatcher) -> eager!
    auto v_custom_sm = base;
    auto* hdr = v_custom_sm.mutable_routes(0)->mutable_match()->add_headers();
    hdr->set_name("x-lua");
    hdr->mutable_string_match()->mutable_custom()->set_name("lua_sm");
    hdr->mutable_string_match()->mutable_custom()->mutable_typed_config()->set_type_url(
        "type.googleapis.com/envoy.extensions.string_matcher.lua.v3.Lua");
    EXPECT_TRUE(requiresMainThreadInit(v_custom_sm));

    // uri_template path_match_policy + path_rewrite_policy -> deferred; uri_template input
    // matcher (which asserts main thread) -> eager!
    envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig ut_match;
    ut_match.set_path_template("/api/{version}/items");
    envoy::extensions::path::rewrite::uri_template::v3::UriTemplateRewriteConfig ut_rewrite;
    ut_rewrite.set_path_template_rewrite("/v2/{version}/items");
    auto v_ut_path = base;
    v_ut_path.mutable_routes(0)->mutable_match()->mutable_path_match_policy()->set_name("ut_match");
    packAny(*v_ut_path.mutable_routes(0)
                 ->mutable_match()
                 ->mutable_path_match_policy()
                 ->mutable_typed_config(),
            ut_match);
    v_ut_path.mutable_routes(0)->mutable_route()->mutable_path_rewrite_policy()->set_name(
        "ut_rewrite");
    packAny(*v_ut_path.mutable_routes(0)
                 ->mutable_route()
                 ->mutable_path_rewrite_policy()
                 ->mutable_typed_config(),
            ut_rewrite);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_ut_path));
#endif

    // rate_limits[].actions[].extension: vetted HttpRequestHeaderMatchInput -> deferred;
    // unvetted expr descriptor -> eager!
    envoy::type::matcher::v3::HttpRequestHeaderMatchInput req_hdr_input;
    req_hdr_input.set_header_name("x-user");
    auto v_rl_ok = base;
    auto* rl_ext = v_rl_ok.add_rate_limits()->add_actions()->mutable_extension();
    rl_ext->set_name("hdr_input");
    packAny(*rl_ext->mutable_typed_config(), req_hdr_input);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_rl_ok));
#endif

    auto v_rl_expr = base;
    auto* expr_ext = v_rl_expr.add_rate_limits()->add_actions()->mutable_extension();
    expr_ext->set_name("expr_desc");
    expr_ext->mutable_typed_config()->set_type_url(
        "type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor");
    EXPECT_TRUE(requiresMainThreadInit(v_rl_expr));

    // Nested filter formatters inside allowlisted SetFilterState per-route config:
    // ReqWithoutQuery -> deferred; GenericSecret -> eager!
    envoy::extensions::filters::http::set_filter_state::v3::Config sfs_cfg;
    auto* field_cfg = sfs_cfg.add_on_request_headers();
    field_cfg->set_object_key("my_key");
    field_cfg->mutable_format_string()->mutable_text_format_source()->set_inline_string(
        "%REQ_WITHOUT_QUERY(:path)%");
    auto* sfs_fmt = field_cfg->mutable_format_string()->add_formatters();
    sfs_fmt->set_name("req_wo_q");
    packAny(*sfs_fmt->mutable_typed_config(), req_wo_query);
    auto v_sfs = base;
    packAny((*v_sfs.mutable_routes(0)->mutable_typed_per_filter_config())["sfs"], sfs_cfg);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_sfs));
#endif

    sfs_fmt->mutable_typed_config()->set_type_url(
        "type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret");
    packAny((*v_sfs.mutable_routes(0)->mutable_typed_per_filter_config())["sfs"], sfs_cfg);
    EXPECT_TRUE(requiresMainThreadInit(v_sfs));

    // Deeply nested (>30 levels) JSON Metadata.filter_metadata Struct short-circuits without
    // hitting kMaxReflectionDepth, whereas Struct packed inside Any still requires main-thread
    // init.
    auto v_deep_struct = base;
    Protobuf::Struct* curr_struct =
        &(*v_deep_struct.mutable_metadata()->mutable_filter_metadata())["envoy.lb"];
    for (int i = 0; i < 30; ++i) {
      curr_struct = (*curr_struct->mutable_fields())["nested"].mutable_struct_value();
    }
    (*curr_struct->mutable_fields())["leaf"].set_string_value("val");
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(v_deep_struct));
#endif

    auto v_any_struct = base;
    Protobuf::Struct raw_struct;
    (*raw_struct.mutable_fields())["k"].set_string_value("v");
    packAny((*v_any_struct.mutable_typed_per_filter_config())["raw"], raw_struct);
    EXPECT_TRUE(requiresMainThreadInit(v_any_struct));
  }

  // 5. Matcher predicates (and/or/not/single input & custom_match), MatcherTree input, RouteList,
  // uri_template input matcher, custom_match tree, custom match action, and recursion depth guard.
  {
    envoy::type::matcher::v3::HttpRequestHeaderMatchInput req_hdr_input;
    req_hdr_input.set_header_name("x-match");

    envoy::config::route::v3::VirtualHost mv;
    mv.set_name("matcher_vhost");
    mv.add_domains("m.example.com");
    mv.mutable_matcher();
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(mv));
#endif

    auto* fm = mv.mutable_matcher()->mutable_matcher_list()->add_matchers();
    auto* not_pred =
        fm->mutable_predicate()->mutable_or_matcher()->add_predicate()->mutable_not_matcher();
    auto* sp = not_pred->mutable_and_matcher()->add_predicate()->mutable_single_predicate();
    sp->mutable_input()->set_name("request-headers");
    packAny(*sp->mutable_input()->mutable_typed_config(), req_hdr_input);
    sp->mutable_value_match()->set_exact("v");
    envoy::config::route::v3::Route inner_route = base.routes(0);
    packAny(*fm->mutable_on_match()->mutable_action()->mutable_typed_config(), inner_route);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(mv));
#endif

    // uri_template input matcher (asserts main thread) or dynamic_modules custom_match -> true.
    auto mv_ut_pred = mv;
    auto* ut_cm = mv_ut_pred.mutable_matcher()
                      ->mutable_matcher_list()
                      ->mutable_matchers(0)
                      ->mutable_predicate()
                      ->mutable_single_predicate()
                      ->mutable_custom_match();
    ut_cm->set_name("ut_input_matcher");
    ut_cm->mutable_typed_config()->set_type_url(
        "type.googleapis.com/envoy.extensions.matching.input_matchers.uri_template.v3.UriTemplate");
    EXPECT_TRUE(requiresMainThreadInit(mv_ut_pred));

    // Dynamic module MatcherTree input -> true.
    envoy::config::route::v3::VirtualHost tree_dm;
    tree_dm.mutable_matcher()->mutable_matcher_tree()->mutable_input()->set_name("dm_input");
    tree_dm.mutable_matcher()
        ->mutable_matcher_tree()
        ->mutable_input()
        ->mutable_typed_config()
        ->set_type_url("type.googleapis.com/"
                       "envoy.extensions.matching.http.dynamic_modules.v3.DynamicModuleDataInput");
    EXPECT_TRUE(requiresMainThreadInit(tree_dm));

    // MatcherTree exact_match_map / prefix_match_map with RouteList containing route_specifiers.
    envoy::config::route::v3::RouteList rl;
    *rl.add_routes() = base.routes(0);
    envoy::config::route::v3::VirtualHost tree_map;
    tree_map.mutable_matcher()->mutable_matcher_tree()->mutable_input()->set_name("hdr");
    packAny(*tree_map.mutable_matcher()
                 ->mutable_matcher_tree()
                 ->mutable_input()
                 ->mutable_typed_config(),
            req_hdr_input);
    packAny(*(*tree_map.mutable_matcher()
                   ->mutable_matcher_tree()
                   ->mutable_prefix_match_map()
                   ->mutable_map())["p"]
                 .mutable_action()
                 ->mutable_typed_config(),
            rl);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresMainThreadInit(tree_map));
#endif

    rl.mutable_routes(0)->add_route_specifiers()->set_name("specifier");
    packAny(*(*tree_map.mutable_matcher()
                   ->mutable_matcher_tree()
                   ->mutable_prefix_match_map()
                   ->mutable_map())["p"]
                 .mutable_action()
                 ->mutable_typed_config(),
            rl);
    EXPECT_TRUE(requiresMainThreadInit(tree_map));

    // custom_match tree and custom action -> conservatively true.
    envoy::config::route::v3::VirtualHost custom_tree;
    custom_tree.mutable_matcher()->mutable_matcher_tree()->mutable_custom_match()->set_name("cm");
    EXPECT_TRUE(requiresMainThreadInit(custom_tree));

    envoy::config::route::v3::VirtualHost custom_action;
    custom_action.mutable_matcher()->mutable_on_no_match()->mutable_action()->set_name("ca");
    custom_action.mutable_matcher()
        ->mutable_on_no_match()
        ->mutable_action()
        ->mutable_typed_config()
        ->set_type_url("type.googleapis.com/google.protobuf.StringValue");
    EXPECT_TRUE(requiresMainThreadInit(custom_action));

    // Pathologically nested FilterConfig (>64 levels) hits depth guard and conservatively returns
    // true.
    envoy::config::route::v3::FilterConfig nested_fc;
    for (int i = 0; i < 70; ++i) {
      envoy::config::route::v3::FilterConfig outer_fc;
      packAny(*outer_fc.mutable_config(), nested_fc);
      nested_fc = std::move(outer_fc);
    }
    auto v_deep = base;
    packAny((*v_deep.mutable_typed_per_filter_config())["deep"], nested_fc);
    EXPECT_TRUE(requiresMainThreadInit(v_deep));
  }
}

TEST_F(DeferredVirtualHostTest, FastPathRequiresProbeValidationInvariantAndBranches) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1", "shadow_cluster"}, {});

  const std::vector<std::string> valid_fast_path_yamls = {
      R"EOF(
virtual_hosts:
- name: prefix_vhost
  domains: ["prefix.example.com"]
  routes:
  - match: { prefix: "/api" }
    route: { cluster: "cluster_1" }
)EOF",
      R"EOF(
virtual_hosts:
- name: exact_and_sep_vhost
  domains: ["exact.example.com"]
  require_tls: EXTERNAL_ONLY
  routes:
  - match: { path: "/v1/item", case_sensitive: false }
    route: { cluster: "cluster_1", priority: HIGH, cluster_not_found_response_code: NOT_FOUND }
  - match: { path_separated_prefix: "/service" }
    route:
      cluster: "cluster_1"
      timeout: { seconds: 15, nanos: 500000000 }
      idle_timeout: { seconds: 30 }
      flush_timeout: { seconds: 45 }
      max_grpc_timeout: { seconds: 20 }
      grpc_timeout_offset: { seconds: 1 }
      max_stream_duration:
        max_stream_duration: { seconds: 300 }
        grpc_timeout_header_max: { seconds: 60 }
        grpc_timeout_header_offset: { seconds: 2 }
)EOF",
      R"EOF(
request_mirror_policies:
- cluster: "shadow_cluster"
virtual_hosts:
- name: shadow_vhost
  domains: ["shadow.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF",
  };

  for (const auto& yaml : valid_fast_path_yamls) {
    const auto rc = parseRouteConfig(yaml);
    auto global_or_error = CommonConfigImpl::create(
        rc, factory_context_, ProtobufMessage::getStrictValidationVisitor(), init_manager_);
    ASSERT_OK(global_or_error.status());
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresProbeValidation(rc.virtual_hosts(0), global_or_error.value(), true,
                                         factory_context_));
#else
    EXPECT_TRUE(requiresProbeValidation(rc.virtual_hosts(0), global_or_error.value(), true,
                                        factory_context_));
#endif
    ASSERT_NE(createConfig(rc), nullptr);
  }

  const auto base_rc = parseRouteConfig(valid_fast_path_yamls[0]);
  auto global_cfg =
      CommonConfigImpl::create(base_rc, factory_context_,
                               ProtobufMessage::getStrictValidationVisitor(), init_manager_)
          .value();
  const auto base_vhost = base_rc.virtual_hosts(0);
  EXPECT_TRUE(requiresProbeValidation(base_vhost, nullptr, true, factory_context_));

  // Unknown global shadow cluster & unknown route cluster depend on validate_clusters.
  {
    const auto shadow_rc = parseRouteConfig(R"EOF(
request_mirror_policies:
- cluster: "unknown_shadow"
virtual_hosts:
- name: v
  domains: ["example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "cluster_1" }
)EOF");
    auto bad_shadow_global =
        CommonConfigImpl::create(shadow_rc, factory_context_,
                                 ProtobufMessage::getStrictValidationVisitor(), init_manager_)
            .value();
    EXPECT_TRUE(requiresProbeValidation(base_vhost, bad_shadow_global, true, factory_context_));
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(requiresProbeValidation(base_vhost, bad_shadow_global, false, factory_context_));
#endif

    auto unknown_cluster_vhost = base_vhost;
    unknown_cluster_vhost.mutable_routes(0)->mutable_route()->set_cluster("unknown_cluster");
    EXPECT_TRUE(requiresProbeValidation(unknown_cluster_vhost, global_cfg, true, factory_context_));
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    EXPECT_FALSE(
        requiresProbeValidation(unknown_cluster_vhost, global_cfg, false, factory_context_));
#endif
  }

  // Unknown wire field on VirtualHost forces probe validation.
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  {
    auto unknown_field_vhost = base_vhost;
    unknown_field_vhost.GetReflection()
        ->MutableUnknownFields(&unknown_field_vhost)
        ->AddVarint(999, 1);
    EXPECT_TRUE(requiresProbeValidation(unknown_field_vhost, global_cfg, true, factory_context_));
  }
#endif

  const std::vector<std::function<void(envoy::config::route::v3::VirtualHost&)>> mutators = {
      [](auto& v) { v.mutable_matcher(); },
      [](auto& v) { v.add_virtual_clusters()->set_name("vc"); },
      [](auto& v) { v.add_rate_limits(); },
      [](auto& v) {
        (*v.mutable_typed_per_filter_config())["f"].set_type_url(
            "type.googleapis.com/google.protobuf.Struct");
      },
      [](auto& v) { v.add_request_mirror_policies()->set_cluster("cluster_1"); },
      [](auto& v) { v.mutable_retry_policy(); },
      [](auto& v) { v.mutable_hedge_policy(); },
      [](auto& v) { v.mutable_metadata(); },
      [](auto& v) { v.add_request_headers_to_add(); },
      [](auto& v) { v.add_request_headers_to_remove("x-remove"); },
      [](auto& v) { v.add_response_headers_to_add(); },
      [](auto& v) { v.add_response_headers_to_remove("x-remove"); },
      [](auto& v) {
        v.set_require_tls(
            static_cast<envoy::config::route::v3::VirtualHost_TlsRequirementType>(-1));
      },
      [](auto& v) { v.mutable_routes(0)->mutable_redirect()->set_host_redirect("other.com"); },
      [](auto& v) { v.mutable_routes(0)->mutable_direct_response()->set_status(200); },
      [](auto& v) { v.mutable_routes(0)->clear_action(); },
      [](auto& v) { v.mutable_routes(0)->add_request_headers_to_add(); },
      [](auto& v) { v.mutable_routes(0)->mutable_metadata(); },
      [](auto& v) { v.mutable_routes(0)->mutable_decorator()->set_operation("op"); },
      [](auto& v) { v.mutable_routes(0)->mutable_tracing(); },
      [](auto& v) { v.mutable_routes(0)->mutable_match()->mutable_safe_regex()->set_regex(".*"); },
      [](auto& v) { v.mutable_routes(0)->mutable_match()->mutable_connect_matcher(); },
      [](auto& v) { v.mutable_routes(0)->mutable_match()->clear_path_specifier(); },
      [](auto& v) { v.mutable_routes(0)->mutable_match()->add_headers()->set_name("hdr"); },
      [](auto& v) { v.mutable_routes(0)->mutable_match()->add_query_parameters()->set_name("q"); },
      [](auto& v) { v.mutable_routes(0)->mutable_route()->clear_cluster(); },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->mutable_weighted_clusters()->add_clusters()->set_name(
            "cluster_1");
      },
      [](auto& v) { v.mutable_routes(0)->mutable_route()->set_cluster_header("x-cluster"); },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->set_cluster_not_found_response_code(
            static_cast<envoy::config::route::v3::RouteAction_ClusterNotFoundResponseCode>(-1));
      },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->set_priority(
            static_cast<envoy::config::core::v3::RoutingPriority>(-1));
      },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->set_internal_redirect_action(
            envoy::config::route::v3::RouteAction::HANDLE_INTERNAL_REDIRECT);
      },
      [](auto& v) { v.mutable_routes(0)->mutable_route()->mutable_timeout()->set_seconds(-1); },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->mutable_idle_timeout()->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->mutable_flush_timeout()->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->mutable_max_grpc_timeout()->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)->mutable_route()->mutable_grpc_timeout_offset()->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)
            ->mutable_route()
            ->mutable_max_stream_duration()
            ->mutable_max_stream_duration()
            ->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)
            ->mutable_route()
            ->mutable_max_stream_duration()
            ->mutable_grpc_timeout_header_max()
            ->set_seconds(-1);
      },
      [](auto& v) {
        v.mutable_routes(0)
            ->mutable_route()
            ->mutable_max_stream_duration()
            ->mutable_grpc_timeout_header_offset()
            ->set_seconds(-1);
      },
      [](auto& v) { v.mutable_routes(0)->mutable_route()->set_prefix_rewrite("/new"); },
      [](auto& v) { v.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy(); },
  };

  for (const auto& mutator : mutators) {
    auto vhost = base_vhost;
    mutator(vhost);
    EXPECT_TRUE(requiresProbeValidation(vhost, global_cfg, true, factory_context_));
  }
}

class ProbingFilterTest : public DeferredVirtualHostTest {
protected:
  class TestSingletonInstance : public Singleton::Instance {};
  struct ContextCapturingRouteConfig : public RouteSpecificFilterConfig {
    explicit ContextCapturingRouteConfig(Server::Configuration::ServerFactoryContext& ctx)
        : context(ctx) {}
    Server::Configuration::ServerFactoryContext& context;
  };

  class TestFilterFactory : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    TestFilterFactory() : EmptyHttpFilterConfig("test.deferred.filter") {}

    absl::StatusOr<Http::FilterFactoryCb>
    createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
      PANIC("not implemented");
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return std::make_unique<envoy::extensions::filters::http::buffer::v3::BufferPerRoute>();
    }
    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<envoy::extensions::filters::http::buffer::v3::BufferPerRoute>();
    }
    std::set<std::string> configTypes() override {
      return {"envoy.extensions.filters.http.buffer.v3.BufferPerRoute"};
    }

    absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr> createHttpFilterRouteConfig(
        const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context,
        Server::Configuration::ExtraFactoryContext& extra_context) override {
      init_managers_.push_back(extra_context.init_manager.ptr());
      context_visitors_.push_back(&context.messageValidationVisitor());
      context_init_managers_.push_back(&context.initManager());
      if (register_target_ && extra_context.init_manager.has_value()) {
        targets_.push_back(std::make_unique<Init::TargetImpl>("test_target", []() {}));
        extra_context.init_manager->add(*targets_.back());
      }
      if (allocate_tls_slot_) {
        auto slot = ThreadLocal::TypedSlot<>::makeUnique(context.threadLocal());
        slot->set(
            [](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return nullptr; });
        slot->runOnAllThreads([](OptRef<ThreadLocal::ThreadLocalObject>) {});
        bool completed = false;
        slot->runOnAllThreads([](OptRef<ThreadLocal::ThreadLocalObject>) {},
                              [&completed]() { completed = true; });
        EXPECT_TRUE(completed);
        slots_.push_back(std::move(slot));
      }
      if (create_pinned_singleton_) {
        created_singletons_.push_back(context.singletonManager().getTyped<TestSingletonInstance>(
            SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_test_singleton),
            []() { return std::make_shared<TestSingletonInstance>(); }, /*pin=*/true));
      }
      if (lookup_parent_singleton_) {
        observed_parent_singleton_ = context.singletonManager().getTyped<TestSingletonInstance>(
            SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_parent_singleton));
      }
      if (custom_context_hook_) {
        custom_context_hook_(context);
      }
      return std::make_shared<ContextCapturingRouteConfig>(context);
    }

    std::vector<Init::Manager*> init_managers_;
    std::vector<ProtobufMessage::ValidationVisitor*> context_visitors_;
    std::vector<Init::Manager*> context_init_managers_;
    bool register_target_{false};
    bool allocate_tls_slot_{false};
    bool create_pinned_singleton_{false};
    bool lookup_parent_singleton_{false};
    std::function<void(Server::Configuration::ServerFactoryContext&)> custom_context_hook_;
    std::vector<std::unique_ptr<Init::TargetImpl>> targets_;
    std::vector<ThreadLocal::TypedSlotPtr<>> slots_;
    std::vector<std::shared_ptr<TestSingletonInstance>> created_singletons_;
    std::shared_ptr<TestSingletonInstance> observed_parent_singleton_;
  };

  class UnvettedFilterFactory : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    UnvettedFilterFactory() : EmptyHttpFilterConfig("test.unvetted.filter") {}

    absl::StatusOr<Http::FilterFactoryCb>
    createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
      PANIC("not implemented");
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return std::make_unique<Protobuf::Timestamp>();
    }
    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<Protobuf::Timestamp>();
    }
    std::set<std::string> configTypes() override { return {"google.protobuf.Timestamp"}; }

    absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr> createHttpFilterRouteConfig(
        const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context,
        Server::Configuration::ExtraFactoryContext& extra_context) override {
      init_managers_.push_back(extra_context.init_manager.ptr());
      return std::make_shared<ContextCapturingRouteConfig>(context);
    }

    std::vector<Init::Manager*> init_managers_;
  };

  TestFilterFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_{
      factory_};
  UnvettedFilterFactory unvetted_factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_unvetted_{
      unvetted_factory_};
};

TEST_F(ProbingFilterTest, IsolatedValidationContextIsolatesAndDetectsMainThreadResources) {
  factory_context_.cluster_manager_.initializeClusters({"baz"}, {});

#if defined(ENVOY_ENABLE_FULL_PROTOS)
  const auto rc = parseRouteConfig(R"EOF(
typed_per_filter_config:
  test.deferred.filter:
    "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
    buffer: { max_request_bytes: 1024 }
virtual_hosts:
- name: bar
  domains: ["bar.example.com"]
  typed_per_filter_config:
    test.deferred.filter:
      "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
      buffer: { max_request_bytes: 2048 }
  routes:
  - match: { prefix: "/" }
    typed_per_filter_config:
      test.deferred.filter:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        buffer: { max_request_bytes: 4096 }
    route:
      weighted_clusters:
        clusters:
        - name: baz
          weight: 100
          typed_per_filter_config:
            test.deferred.filter:
              "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
              buffer: { max_request_bytes: 8192 }
)EOF");
  // 1. Pure worker-safe filter config: isolated probe validation runs once, virtual host stays
  // deferred even after transient RDS Init::Manager is destroyed, and inflates on worker thread
  // with WorkerSafeFactoryContext (null ValidationVisitor & isolated Init::Manager) whose lifetime
  // outlives RouteMatcher/ConfigImpl via the returned RouteConstSharedPtr.
  {
    RouteConstSharedPtr retained_route;
    Init::Manager* transient_addr = nullptr;
    {
      std::shared_ptr<ConfigImpl> config;
      {
        NiceMock<Init::MockManager> transient_rds_init_manager;
        transient_addr = &transient_rds_init_manager;
        auto status_or =
            ConfigImpl::create(rc, factory_context_, ProtobufMessage::getNullValidationVisitor(),
                               transient_rds_init_manager, true);
        ASSERT_OK(status_or.status());
        config = std::move(status_or.value());
        ASSERT_EQ(factory_.init_managers_.size(), 4);
        EXPECT_EQ(factory_.init_managers_[0], transient_addr);
        EXPECT_NE(factory_.init_managers_[1], transient_addr);
        EXPECT_EQ(factory_.init_managers_[1], factory_.init_managers_[2]);
        EXPECT_EQ(factory_.init_managers_[2], factory_.init_managers_[3]);
      }

      std::thread worker([&]() {
        NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
        retained_route = config->route(genHeaders("bar.example.com", "/"), local_info, 0).route;
        EXPECT_NE(retained_route, nullptr);
      });
      worker.join();
      ASSERT_EQ(factory_.init_managers_.size(), 7);
      EXPECT_NE(factory_.init_managers_[4], transient_addr);
      EXPECT_NE(factory_.context_init_managers_[4], &factory_context_.initManager());
      EXPECT_EQ(factory_.context_visitors_[4], &ProtobufMessage::getNullValidationVisitor());
    }
    // ConfigImpl and RouteMatcher are now destroyed; verify captured ServerFactoryContext& on the
    // retained RouteConstSharedPtr remains valid.
    ASSERT_NE(retained_route, nullptr);
    const auto* captured = retained_route->mostSpecificPerFilterConfig("test.deferred.filter");
    ASSERT_NE(captured, nullptr);
    const auto* typed_captured = dynamic_cast<const ContextCapturingRouteConfig*>(captured);
    ASSERT_NE(typed_captured, nullptr);
    EXPECT_EQ(&typed_captured->context.messageValidationVisitor(),
              &ProtobufMessage::getNullValidationVisitor());
    EXPECT_EQ(&typed_captured->context.runtime(), &factory_context_.runtime());
  }

  const auto single_filter_rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: bar
  domains: ["bar.example.com"]
  routes:
  - match: { prefix: "/" }
    typed_per_filter_config:
      test.deferred.filter:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        buffer: { max_request_bytes: 1024 }
    route: { cluster: "baz" }
)EOF");

  // 2. Init::Target registration during probe forces eager main-thread initialization.
  {
    factory_.init_managers_.clear();
    factory_.register_target_ = true;
    EXPECT_CALL(init_manager_, add(_));
    auto matcher = createMatcher(single_filter_rc);
    factory_.register_target_ = false;
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.init_managers_.size(), 2);
    EXPECT_NE(factory_.init_managers_[0], &init_manager_);
    EXPECT_EQ(factory_.init_managers_[1], &init_manager_);
  }

  // 3. ThreadLocal slot allocation uses isolated NoOpSlot (never touching parent threadLocal()) and
  // forces eager main-thread initialization.
  {
    factory_.init_managers_.clear();
    factory_.slots_.clear();
    factory_.allocate_tls_slot_ = true;
    EXPECT_CALL(factory_context_.thread_local_, allocateSlot()); // Only eager fallback!
    auto matcher = createMatcher(single_filter_rc);
    factory_.allocate_tls_slot_ = false;
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.slots_.size(), 2);
    EXPECT_FALSE(factory_.slots_[0]->currentThreadRegistered());
    EXPECT_FALSE(factory_.slots_[0]->get().has_value());
    EXPECT_FALSE(factory_.slots_[0]->isShutdown());
  }

  // 4. Singleton creation during probe stays isolated in local_manager_ (even when pin=true!) so
  // parent singletonManager() only receives the eager main-thread instance; non-constructing
  // lookups still see pre-existing parent singletons.
  {
    auto parent_singleton = factory_context_.singletonManager().getTyped<TestSingletonInstance>(
        SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_parent_singleton),
        []() { return std::make_shared<TestSingletonInstance>(); }, /*pin=*/true);
    ASSERT_NE(parent_singleton, nullptr);

    factory_.init_managers_.clear();
    factory_.created_singletons_.clear();
    factory_.create_pinned_singleton_ = true;
    factory_.lookup_parent_singleton_ = true;
    auto matcher = createMatcher(single_filter_rc);
    factory_.create_pinned_singleton_ = false;
    factory_.lookup_parent_singleton_ = false;
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.created_singletons_.size(), 2);
    // Probe singleton and eager singleton are distinct because probe never polluted parent!
    EXPECT_NE(factory_.created_singletons_[0], factory_.created_singletons_[1]);
    EXPECT_EQ(factory_context_.singletonManager().getTyped<TestSingletonInstance>(
                  SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_test_singleton)),
              factory_.created_singletons_[1]);
    EXPECT_EQ(factory_.observed_parent_singleton_, parent_singleton);
  }

  // 5. All main-thread-bound primitives on IsolatedValidationContext (including
  // mainThreadDispatcher, clusterManager, accessLogManager, api().allocateDispatcher(), and
  // api().customStatNamespaces()) force eager main-thread initialization.
  const std::vector<std::function<void(Server::Configuration::ServerFactoryContext&)>>
      main_thread_hooks = {
          [](auto& ctx) { std::ignore = ctx.secretManager(); },
          [](auto& ctx) { std::ignore = ctx.clusterManager(); },
          [](auto& ctx) { std::ignore = ctx.xdsManager(); },
          [](auto& ctx) { std::ignore = ctx.mainThreadDispatcher(); },
          [](auto& ctx) { std::ignore = ctx.admin(); },
          [](auto& ctx) { std::ignore = ctx.accessLogManager(); },
          [](auto& ctx) { std::ignore = ctx.drainManager(); },
          [](auto& ctx) { std::ignore = ctx.lifecycleNotifier(); },
          [](auto& ctx) { std::ignore = ctx.sslContextManager(); },
          [](auto& ctx) { std::ignore = ctx.api().allocateDispatcher("probe_disp"); },
          [](auto& ctx) {
            std::ignore = ctx.api().allocateDispatcher(
                "probe_disp_st", Event::ScaledRangeTimerManagerFactory{
                                     [](Event::Dispatcher&) -> Event::ScaledRangeTimerManagerPtr {
                                       return nullptr;
                                     }});
          },
          [](auto& ctx) {
            std::ignore =
                ctx.api().allocateDispatcher("probe_disp_wm", Buffer::WatermarkFactoryPtr{});
          },
          [](auto& ctx) { std::ignore = ctx.api().customStatNamespaces(); },
      };
  for (const auto& hook : main_thread_hooks) {
    factory_.init_managers_.clear();
    factory_.custom_context_hook_ = hook;
    auto matcher = createMatcher(single_filter_rc);
    factory_.custom_context_hook_ = nullptr;
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.init_managers_.size(), 2);
    EXPECT_EQ(factory_.init_managers_[1], &init_manager_);
  }

  // 5b. Worker-safe DelegatingServerFactoryContext and TrackingApi accessors keep the virtual host
  // deferred (and isolate api().rootScope() from parent stats).
  {
    factory_.init_managers_.clear();
    factory_.custom_context_hook_ = [&](Server::Configuration::ServerFactoryContext& ctx) {
      EXPECT_EQ(&ctx.scope(), &ctx.serverScope());
      EXPECT_EQ(&ctx.api().rootScope(), &ctx.scope());
      EXPECT_NE(&ctx.api().rootScope(), &factory_context_.api().rootScope());
      std::ignore = ctx.httpServerPropertiesCacheManager();
      std::ignore = ctx.options();
      std::ignore = ctx.localInfo();
      std::ignore = ctx.messageValidationContext();
      std::ignore = ctx.runtime();
      std::ignore = ctx.timeSource();
      std::ignore = ctx.httpContext();
      std::ignore = ctx.grpcContext();
      std::ignore = ctx.routerContext();
      std::ignore = ctx.processContext();
      std::ignore = ctx.regexEngine();
      std::ignore = ctx.statsConfig();
      std::ignore = ctx.bootstrap();
      std::ignore = ctx.overloadManager();
      std::ignore = ctx.nullOverloadManager();
      std::ignore = ctx.healthCheckFailed();
      std::ignore = ctx.api().threadFactory();
      std::ignore = ctx.api().fileSystem();
      std::ignore = ctx.api().timeSource();
      std::ignore = ctx.api().randomGenerator();
      std::ignore = ctx.api().processContext();
      std::ignore = ctx.api().bootstrap();
    };
    auto matcher = createMatcher(single_filter_rc);
    factory_.custom_context_hook_ = nullptr;
    ASSERT_NE(matcher, nullptr);
    EXPECT_TRUE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.init_managers_.size(), 1);
  }

  // 5c. Unvetted Any type URLs (not in the allowlist) skip probe_vhost completely and
  // initialize eagerly once on the main thread.
  {
    const auto unvetted_rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: unvetted_bar
  domains: ["unvetted.example.com"]
  routes:
  - match: { prefix: "/" }
    typed_per_filter_config:
      test.unvetted.filter:
        "@type": type.googleapis.com/google.protobuf.Timestamp
        value: "1970-01-01T00:00:01Z"
    route: { cluster: "baz" }
)EOF");
    unvetted_factory_.init_managers_.clear();
    auto matcher = createMatcher(unvetted_rc);
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("unvetted.example.com")->isDeferred());
    ASSERT_EQ(unvetted_factory_.init_managers_.size(), 1);
    EXPECT_EQ(unvetted_factory_.init_managers_[0], &init_manager_);
  }
#endif

  // 6. Concurrent worker-thread inflation of matcher-based virtual hosts when parent
  // ServerFactoryContext::messageValidationVisitor() is an unsynchronized WarningValidationVisitor
  // never touches WarningValidationVisitor on worker threads.
  {
    class MainThreadOnlyWarningVisitor : public ProtobufMessage::WarningValidationVisitorImpl {
    public:
      const std::thread::id main_id{std::this_thread::get_id()};
      std::atomic<int> off_thread_calls{0};
      bool skipValidation() override {
        if (std::this_thread::get_id() != main_id) {
          off_thread_calls.fetch_add(1, std::memory_order_relaxed);
        }
        return WarningValidationVisitorImpl::skipValidation();
      }
      absl::Status onUnknownField(absl::string_view description) override {
        if (std::this_thread::get_id() != main_id) {
          off_thread_calls.fetch_add(1, std::memory_order_relaxed);
        }
        return WarningValidationVisitorImpl::onUnknownField(description);
      }
    } warning_visitor;
    ON_CALL(factory_context_, messageValidationVisitor()).WillByDefault(ReturnRef(warning_visitor));

    envoy::config::route::v3::RouteConfiguration matcher_rc;
    constexpr int kNumVhosts = 16;
    for (int i = 0; i < kNumVhosts; ++i) {
      auto* vh = matcher_rc.add_virtual_hosts();
      vh->set_name(fmt::format("mv_{}", i));
      vh->add_domains(fmt::format("m{}.example.com", i));
      envoy::config::route::v3::Route action_route;
      action_route.mutable_match()->set_prefix("/");
      action_route.mutable_route()->set_cluster("baz");
#if defined(ENVOY_ENABLE_FULL_PROTOS)
      action_route.GetReflection()->MutableUnknownFields(&action_route)->AddVarint(777 + i, 1);
#endif
      packAny(
          *vh->mutable_matcher()->mutable_on_no_match()->mutable_action()->mutable_typed_config(),
          action_route);
    }

    auto config = createConfig(matcher_rc);
    ASSERT_NE(config, nullptr);

    std::atomic<bool> start_gate{false};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
      workers.emplace_back([&, t]() {
        while (!start_gate.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
        for (int i = 0; i < kNumVhosts; ++i) {
          const int idx = (i + t) % kNumVhosts;
          auto route =
              config->route(genHeaders(fmt::format("m{}.example.com", idx), "/"), local_info, 0);
          EXPECT_NE(route.route, nullptr);
        }
      });
    }
    start_gate.store(true, std::memory_order_release);
    for (auto& w : workers) {
      w.join();
    }
    EXPECT_EQ(warning_visitor.off_thread_calls.load(), 0);

    ON_CALL(factory_context_, messageValidationVisitor())
        .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  }

#if defined(USE_CEL_PARSER)
  // 7. %CEL(...)% formatter accesses ServerFactoryContextInstance::get(), which
  // ScopedValidationServerContext tracks to force eager main-thread initialization while restoring
  // ServerFactoryContextInstance afterwards.
  {
    ScopedThreadLocalServerContextSetter server_context_setter(factory_context_);
    const auto cel_rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: cel_vhost
  domains: ["cel.example.com"]
  request_headers_to_add:
  - header:
      key: "x-cel"
      value: "%CEL(request.headers['x-version'])%"
  routes:
  - match: { prefix: "/" }
    route: { cluster: "baz" }
)EOF");
    auto config = createConfig(cel_rc);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(Server::Configuration::ServerFactoryContextInstance::getExisting(),
              &factory_context_);

    std::thread worker([&config]() {
      auto headers = genHeaders("cel.example.com", "/");
      headers.addCopy("x-version", "v2");
      NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
      auto route = config->route(headers, local_info, 0);
      ASSERT_NE(route.route, nullptr);
      const Formatter::Context formatter_context{&headers};
      route.route->routeEntry()->finalizeRequestHeaders(headers, formatter_context, local_info,
                                                        true);
      EXPECT_EQ("v2", headers.get_(Http::LowerCaseString("x-cel")));
    });
    worker.join();
  }
#endif
}

TEST_F(DeferredVirtualHostTest, WorkerThreadLegacyHeaderTranslation) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1"}, {});

  const auto rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: legacy_headers_vhost
  domains: ["headers.example.com"]
  request_headers_to_add:
  - header:
      key: "x-translated"
      value: "%UPSTREAM_METADATA([\"a\", \"b\"])% %PER_REQUEST_STATE(my_state)%"
  request_mirror_policies:
  - cluster: "cluster_1"
    request_headers_mutations:
    - append:
        header:
          key: "x-vhost-mirror-translated"
          value: "%DYNAMIC_METADATA([\"m\", \"n\"])%"
  routes:
  - match: { prefix: "/" }
    route:
      weighted_clusters:
        clusters:
        - name: "cluster_1"
          weight: 100
          request_headers_to_add:
          - header:
              key: "x-weighted-translated"
              value: "%UPSTREAM_METADATA([\"c\", \"d\"])%"
    request_headers_to_add:
    - header:
        key: "x-route-translated"
        value: "%DYNAMIC_METADATA([\"x\", \"y\"])%"
- name: legacy_matcher_vhost
  domains: ["matcher-headers.example.com"]
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          "@type": type.googleapis.com/envoy.config.route.v3.Route
          match: { prefix: "/" }
          route: { cluster: "cluster_1" }
          request_headers_to_add:
          - header:
              key: "x-matcher-translated"
              value: "%DYNAMIC_METADATA([\"r\", \"1\"])% %PER_REQUEST_STATE(ms)%"
)EOF");

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);

  // Inflate both deferred virtual hosts (with legacy %UPSTREAM_METADATA% / %DYNAMIC_METADATA% JSON
  // parsing) on a non-main worker thread to verify neither triggers main-thread assertions.
  std::thread worker([&config]() {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
    auto h1 = genHeaders("headers.example.com", "/");
    auto r1 = config->route(h1, local_info, 0);
    ASSERT_NE(r1.route, nullptr);
    EXPECT_EQ("legacy_headers_vhost", r1.route->virtualHost().name());

    auto h2 = genHeaders("matcher-headers.example.com", "/");
    auto r2 = config->route(h2, local_info, 0);
    ASSERT_NE(r2.route, nullptr);
    EXPECT_EQ("legacy_matcher_vhost", r2.route->virtualHost().name());
  });
  worker.join();
}

TEST_F(DeferredVirtualHostTest, StaticDirectResponseBodyRemainsDeferredAndFormatsOnWorkerThread) {
  const auto rc = parseRouteConfig(R"EOF(
virtual_hosts:
- name: direct_vhost
  domains: ["direct.example.com"]
  routes:
  - match: { path: "/raw" }
    direct_response:
      status: 200
      body: { inline_string: "static-payload" }
  - match: { path: "/formatted" }
    direct_response:
      status: 200
      body: { inline_string: "base-body" }
      body_format:
        text_format_source: { inline_string: "%LOCAL_REPLY_BODY%:%REQ(x-tag)%" }
)EOF");

  EXPECT_CALL(factory_context_, mainThreadDispatcher()).Times(0);
  auto matcher = createMatcher(rc);
  ASSERT_NE(matcher, nullptr);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  EXPECT_TRUE(matcher->findDomainEntry("direct.example.com")->isDeferred());
#endif

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);

  std::thread worker([&config]() {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
    Http::TestResponseHeaderMapImpl response_headers;
    std::string body_out;

    auto h_raw = genHeaders("direct.example.com", "/raw");
    auto r_raw = config->route(h_raw, local_info, 0);
    ASSERT_NE(r_raw.route, nullptr);
    ASSERT_NE(r_raw.route->directResponseEntry(), nullptr);
    EXPECT_EQ("static-payload", r_raw.route->directResponseEntry()->formatBody(
                                    h_raw, response_headers, local_info, body_out));

    auto h_fmt = genHeaders("direct.example.com", "/formatted");
    h_fmt.addCopy("x-tag", "ok");
    auto r_fmt = config->route(h_fmt, local_info, 0);
    ASSERT_NE(r_fmt.route, nullptr);
    ASSERT_NE(r_fmt.route->directResponseEntry(), nullptr);
    EXPECT_EQ("base-body:ok", r_fmt.route->directResponseEntry()->formatBody(
                                  h_fmt, response_headers, local_info, body_out));
  });
  worker.join();
}

TEST_F(DeferredVirtualHostTest, RejectsInvalidConfigurationsAtIngestionTime) {
  factory_context_.cluster_manager_.initializeClusters(
      {"existing_cluster", "some_cluster", "valid_cluster"}, {});

  struct InvalidCase {
    std::string name;
    std::string yaml;
    std::string expected_error;
  };

  const std::vector<InvalidCase> cases = {
      {"UnknownCluster", R"EOF(
virtual_hosts:
- name: invalid_vhost
  domains: ["invalid.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "non_existent_cluster" }
)EOF",
       "route: unknown cluster 'non_existent_cluster'"},
      {"InvalidRegex", R"EOF(
virtual_hosts:
- name: matcher_vhost
  domains: ["matcher.example.com"]
  routes:
  - match: { safe_regex: { regex: "[" } }
    route: { cluster: "some_cluster" }
)EOF",
       "missing ]"},
      {"MissingClusterSpecifierPlugin", R"EOF(
virtual_hosts:
- name: vhost
  domains: ["csp.example.com"]
  routes:
  - match: { prefix: "/" }
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: "custom"
          typed_config: { "@type": "type.googleapis.com/google.protobuf.Struct" }
)EOF",
       "Didn't find a registered implementation for 'custom'"},
      {"ConflictingPathRewrite", R"EOF(
virtual_hosts:
- name: invalid_policy_vhost
  domains: ["invalid-policy.example.com"]
  routes:
  - match: { prefix: "/test" }
    route:
      cluster: "some_cluster"
      prefix_rewrite: "/rewritten"
      regex_rewrite:
        pattern: { regex: "^/test/(.*)" }
        substitution: "/\\1"
)EOF",
       "Specify only one of prefix_rewrite, regex_rewrite or path_rewrite_policy"},
      {"NegativeTimeout", R"EOF(
virtual_hosts:
- name: negative_timeout_vhost
  domains: ["timeout.example.com"]
  routes:
  - match: { prefix: "/" }
    route:
      cluster: "valid_cluster"
      timeout: { seconds: -5 }
)EOF",
       "Expected positive duration"},
      {"MissingGlobalShadowCluster", R"EOF(
request_mirror_policies:
- cluster: "missing_shadow_cluster"
virtual_hosts:
- name: vhost_with_global_shadow
  domains: ["shadow.example.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: "valid_cluster" }
)EOF",
       "route: unknown shadow cluster 'missing_shadow_cluster'"},
      {"MissingRouteShadowCluster", R"EOF(
virtual_hosts:
- name: vhost_with_route_shadow
  domains: ["route-shadow.example.com"]
  routes:
  - match: { prefix: "/" }
    route:
      cluster: "valid_cluster"
      request_mirror_policies:
      - cluster: "missing_route_shadow_cluster"
)EOF",
       "route: unknown shadow cluster 'missing_route_shadow_cluster'"},
      {"OversizedDirectResponseBody", R"EOF(
max_direct_response_body_size_bytes: 4
virtual_hosts:
- name: vhost_oversized_direct
  domains: ["oversized.example.com"]
  routes:
  - match: { prefix: "/" }
    direct_response:
      status: 200
      body: { inline_string: "too-long-body" }
)EOF",
       "response body size is 13 bytes; maximum is 4"},
      {"InvalidVirtualHostHeader", R"EOF(
virtual_hosts:
- name: bad_hdr_vhost
  domains: ["hdr.example.com"]
  request_headers_to_add:
  - header: { key: ":path", value: "/bad" }
  routes:
  - match: { prefix: "/" }
    route: { cluster: "valid_cluster" }
)EOF",
       ":-prefixed or host headers may not be modified"},
      {"UnknownVirtualHostPerFilterConfig", R"EOF(
virtual_hosts:
- name: bad_filter_vhost
  domains: ["filter.example.com"]
  typed_per_filter_config:
    unknown.non_optional.filter:
      "@type": type.googleapis.com/google.protobuf.BoolValue
  routes:
  - match: { prefix: "/" }
    route: { cluster: "valid_cluster" }
)EOF",
       "Didn't find a registered implementation for 'unknown.non_optional.filter'"},
      {"UnknownVettedVirtualHostPerFilterConfig", R"EOF(
virtual_hosts:
- name: bad_vetted_filter_vhost
  domains: ["vetted-filter.example.com"]
  typed_per_filter_config:
    unknown.non_optional.buffer_filter:
      "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
      disabled: true
  routes:
  - match: { prefix: "/" }
    route: { cluster: "valid_cluster" }
)EOF",
       "Didn't find a registered implementation for 'unknown.non_optional.buffer_filter'"},
  };

  for (const auto& tc : cases) {
    SCOPED_TRACE(tc.name);
    const auto rc = parseRouteConfig(tc.yaml);
    try {
      auto status_or = ConfigImpl::create(
          rc, factory_context_, ProtobufMessage::getNullValidationVisitor(), init_manager_, true);
      EXPECT_FALSE(status_or.ok());
      if (!status_or.ok()) {
        EXPECT_THAT(status_or.status().message(), HasSubstr(tc.expected_error));
      }
    } catch (const EnvoyException& e) {
      EXPECT_THAT(e.what(), HasSubstr(tc.expected_error));
    }
  }
}
#endif

} // namespace
} // namespace Router
} // namespace Envoy
