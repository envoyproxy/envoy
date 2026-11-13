#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
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
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using RouteConfigProto = envoy::config::route::v3::RouteConfiguration;
using VHostProto = envoy::config::route::v3::VirtualHost;
using RouteProto = envoy::config::route::v3::Route;
using ActionProto = envoy::config::route::v3::RouteAction;

#if defined(ENVOY_ENABLE_FULL_PROTOS)
constexpr bool kFullProtosEnabled = true;
#else
constexpr bool kFullProtosEnabled = false;
#endif

SINGLETON_MANAGER_REGISTRATION(deferred_vhost_test_singleton);
SINGLETON_MANAGER_REGISTRATION(deferred_vhost_parent_singleton);

Protobuf::Any makeAny(const Protobuf::Message& message) {
  Protobuf::Any any;
  any.set_type_url(TypeUtil::descriptorFullNameToTypeUrl(message.GetTypeName()));
  RELEASE_ASSERT(message.SerializeToString(any.mutable_value()), "Failed to serialize Any");
  return any;
}

Protobuf::Any makeTypeAny(absl::string_view type_name, absl::string_view value = "") {
  Protobuf::Any any;
  any.set_type_url(TypeUtil::descriptorFullNameToTypeUrl(type_name));
  any.set_value(std::string(value));
  return any;
}

Protobuf::Any makeBufferAny(uint32_t max_bytes) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute buf;
  buf.mutable_buffer()->mutable_max_request_bytes()->set_value(max_bytes);
  return makeAny(buf);
}

template <typename Ext>
void setExtension(Ext* ext, absl::string_view name, Protobuf::Any typed_config) {
  ext->set_name(std::string(name));
  *ext->mutable_typed_config() = std::move(typed_config);
}

Protobuf::Any makeFilterConfigAny(bool disabled, Protobuf::Any inner = {}) {
  envoy::config::route::v3::FilterConfig fc;
  fc.set_disabled(disabled);
  if (!inner.type_url().empty()) {
    *fc.mutable_config() = std::move(inner);
  }
  return makeAny(fc);
}

void addHeaderOption(
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>* headers,
    absl::string_view key, absl::string_view value) {
  auto* hdr = headers->Add()->mutable_header();
  hdr->set_key(std::string(key));
  hdr->set_value(std::string(value));
}

VHostProto makeBaseVhost(absl::string_view name = "vhost", absl::string_view domain = "example.com",
                         absl::string_view cluster = "cluster_1") {
  VHostProto vhost;
  vhost.set_name(std::string(name));
  vhost.add_domains(std::string(domain));
  auto* route = vhost.add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_route()->set_cluster(std::string(cluster));
  return vhost;
}

RouteConfigProto makeRouteConfig(std::initializer_list<VHostProto> vhosts,
                                 absl::string_view name = "test_rc") {
  RouteConfigProto rc;
  rc.set_name(std::string(name));
  for (const auto& vh : vhosts) {
    *rc.add_virtual_hosts() = vh;
  }
  return rc;
}

template <typename Fn> VHostProto modVh(Fn&& fn) {
  VHostProto vhost = makeBaseVhost();
  fn(vhost);
  return vhost;
}

template <typename Fn> VHostProto modRt(Fn&& fn) {
  return modVh([&](VHostProto& v) { fn(*v.mutable_routes(0)); });
}

template <typename Fn> VHostProto modAct(Fn&& fn) {
  return modRt([&](RouteProto& r) { fn(*r.mutable_route()); });
}

VHostProto vhostWithFilter(Protobuf::Any config, absl::string_view key = "f") {
  return modVh(
      [&](VHostProto& v) { (*v.mutable_typed_per_filter_config())[std::string(key)] = config; });
}

VHostProto routeWithFilter(Protobuf::Any config, absl::string_view key = "f") {
  return modRt(
      [&](RouteProto& r) { (*r.mutable_typed_per_filter_config())[std::string(key)] = config; });
}

VHostProto routeWithFormatter(absl::string_view label, Protobuf::Any config) {
  return modRt([&](RouteProto& r) {
    r.clear_route();
    setExtension(r.mutable_direct_response()->mutable_body_format()->add_formatters(), label,
                 std::move(config));
  });
}

envoy::config::route::v3::DirectResponseAction*
addDirectRoute(VHostProto& vh, absl::string_view path, bool exact_path) {
  auto* r = vh.add_routes();
  if (exact_path) {
    r->mutable_match()->set_path(std::string(path));
  } else {
    r->mutable_match()->set_prefix(std::string(path));
  }
  auto* dr = r->mutable_direct_response();
  dr->set_status(200);
  return dr;
}

void exerciseNoOpInitManager(Init::Manager& mgr, Init::Manager::State expected_state) {
  EXPECT_EQ(mgr.state(), expected_state);
  bool ready = false;
  Init::WatcherImpl watcher("w", [&]() { ready = true; });
  mgr.updateWatcher(watcher);
  mgr.initialize(watcher);
  EXPECT_TRUE(ready);
  envoy::admin::v3::UnreadyTargetsDumps dumps;
  mgr.dumpUnreadyTargets(dumps);
}

void runConcurrently(int num_threads, const std::function<void(int)>& work) {
  std::atomic<bool> start_gate{false};
  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      while (!start_gate.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      work(t);
    });
  }
  start_gate.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }
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

class DeferredVirtualHostTest : public testing::Test {
protected:
  DeferredVirtualHostTest() : api_(Api::createApiForTest()) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
    factory_context_.bootstrap_.mutable_route_manager()->set_enable_deferred_virtual_host_creation(
        true);
  }

  std::shared_ptr<ConfigImpl> createConfig(const RouteConfigProto& rc,
                                           bool validate_clusters = true) {
    auto config_or_error =
        ConfigImpl::create(rc, factory_context_, ProtobufMessage::getNullValidationVisitor(),
                           init_manager_, validate_clusters);
    EXPECT_OK(config_or_error.status());
    return config_or_error.ok() ? std::move(config_or_error.value()) : nullptr;
  }

  std::unique_ptr<RouteMatcher> createMatcher(const RouteConfigProto& rc,
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

TEST_F(DeferredVirtualHostTest, DomainEntrySharedOwnershipAndConcurrentInflation) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1"}, {});
  const auto rc = makeRouteConfig({makeBaseVhost("concurrent_vhost", "concurrent.example.com")});

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

  VHostProto invalid_vhost =
      modRt([](RouteProto& r) { r.mutable_match()->clear_path_specifier(); });
  DomainEntry bad_entry;
  bad_entry.initDeferred(&invalid_vhost);
  EXPECT_TRUE(bad_entry.isDeferred());
  EXPECT_EQ(bad_entry.activeVirtualHost(), nullptr);
  EXPECT_EQ(
      bad_entry.getOrCreateVirtualHost(global_or_error.value(), factory_context_, vhost_scope),
      nullptr);
  EXPECT_EQ(bad_entry.activeVirtualHost(), nullptr);

  // Returned VirtualHostImplConstSharedPtr keeps VirtualHostImpl alive after RouteMatcher is
  // destroyed (eviction-ready lifetime invariant).
  VirtualHostImplConstSharedPtr retained_vhost;
  {
    auto matcher = createMatcher(rc);
    ASSERT_NE(matcher, nullptr);
    retained_vhost = matcher->findVirtualHost("concurrent.example.com");
    ASSERT_NE(retained_vhost, nullptr);
  }
  EXPECT_EQ("concurrent_vhost", retained_vhost->virtualHost()->name());
  retained_vhost.reset();

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  constexpr int kNumThreads = 8;
  constexpr int kIterations = 100;
  std::atomic<int> success_count{0};
  runConcurrently(kNumThreads, [&](int) {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
    for (int j = 0; j < kIterations; ++j) {
      const auto route =
          config->route(genHeaders("concurrent.example.com", "/test"), local_info, 0);
      if (route.route != nullptr && route.route->routeEntry()->clusterName() == "cluster_1") {
        success_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });
  EXPECT_EQ(kNumThreads * kIterations, success_count.load());
}

TEST_F(DeferredVirtualHostTest, ShallowRequiresMainThreadInitTable) {
  constexpr absl::string_view kDmType =
      "envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilterPerRoute";
  constexpr absl::string_view kBufferType =
      "envoy.extensions.filters.http.buffer.v3.BufferPerRoute";
  constexpr absl::string_view kUnlinkedVettedType =
      "envoy.extensions.filters.http.compressor.v3.CompressorPerRoute";
  constexpr absl::string_view kGenericSecretType =
      "envoy.extensions.formatter.generic_secret.v3.GenericSecret";

  envoy::extensions::formatter::req_without_query::v3::ReqWithoutQuery req_wo_query;
  envoy::type::matcher::v3::HttpRequestHeaderMatchInput req_hdr_input;
  req_hdr_input.set_header_name("x-check");
  const auto base_route = makeBaseVhost().routes(0);

  auto makeSessionAny = [](Protobuf::Any state) {
    envoy::extensions::filters::http::stateful_session::v3::StatefulSessionPerRoute ss;
    setExtension(ss.mutable_stateful_session()->mutable_session_state(), "ss", std::move(state));
    return makeAny(ss);
  };
  auto makeSfsAny = [](Protobuf::Any fmt) {
    envoy::extensions::filters::http::set_filter_state::v3::Config sfs;
    setExtension(sfs.add_on_request_headers()->mutable_format_string()->add_formatters(), "fmt",
                 std::move(fmt));
    return makeAny(sfs);
  };
  xds::type::v3::TypedStruct xds_ts;
  xds_ts.set_type_url(TypeUtil::descriptorFullNameToTypeUrl(kBufferType));
  udpa::type::v1::TypedStruct udpa_fc;
  udpa_fc.set_type_url("type.googleapis.com/envoy.config.route.v3.FilterConfig");
  (*udpa_fc.mutable_value()->mutable_fields())["disabled"].set_bool_value(true);
  envoy::extensions::filters::http::on_demand::v3::PerRouteConfig od_cfg;
  od_cfg.mutable_odcds()->mutable_source()->set_path("/tmp/odcds.yaml");
  envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit local_rl;
  local_rl.set_stat_prefix("local_rl");
  envoy::extensions::http::stateful_session::header::v3::HeaderBasedSessionState hdr_session;
  hdr_session.set_name("x-session");
  envoy::config::route::v3::FilterConfig empty_inner_fc;
  empty_inner_fc.mutable_config();
  Protobuf::Any deep_fc = makeFilterConfigAny(false);
  for (int i = 0; i < 70; ++i) {
    deep_fc = makeFilterConfigAny(false, std::move(deep_fc));
  }

  auto makeMatcherListVhost = [&](absl::string_view input_label, bool nested_bool,
                                  Protobuf::Any custom_match = {}) {
    VHostProto v;
    v.set_name("matcher_vhost");
    v.add_domains("dmlabel.example.com");
    auto* fm = v.mutable_matcher()->mutable_matcher_list()->add_matchers();
    auto* pred = fm->mutable_predicate();
    auto* sp = nested_bool ? pred->mutable_or_matcher()
                                 ->add_predicate()
                                 ->mutable_not_matcher()
                                 ->mutable_and_matcher()
                                 ->add_predicate()
                                 ->mutable_single_predicate()
                           : pred->mutable_single_predicate();
    setExtension(sp->mutable_input(), input_label, makeAny(req_hdr_input));
    if (!custom_match.type_url().empty()) {
      setExtension(sp->mutable_custom_match(), "cm", std::move(custom_match));
    } else {
      sp->mutable_value_match()->set_exact("1");
    }
    setExtension(fm->mutable_on_match()->mutable_action(), "route", makeAny(base_route));
    return v;
  };
  auto makeTreeMapVhost = [&](bool with_route_specifiers) {
    return modVh([&](VHostProto& v) {
      v.clear_routes();
      envoy::config::route::v3::RouteList rl;
      *rl.add_routes() = base_route;
      if (with_route_specifiers) {
        rl.mutable_routes(0)->add_route_specifiers()->set_name("specifier");
      }
      auto* tree = v.mutable_matcher()->mutable_matcher_tree();
      setExtension(tree->mutable_input(), "hdr", makeAny(req_hdr_input));
      setExtension((*tree->mutable_prefix_match_map()->mutable_map())["p"].mutable_action(), "rl",
                   makeAny(rl));
    });
  };

  struct Case {
    absl::string_view name;
    bool eager_when_full_protos;
    VHostProto vhost;
  };
  const Case cases[] = {
      {"BaseSimpleRoute", false, makeBaseVhost()},
      {"VhostRouteSpecifiers", true,
       modVh([](auto& v) { v.add_route_specifiers()->set_name("s"); })},
      {"RouteRouteSpecifiers", true,
       modRt([](auto& r) { r.add_route_specifiers()->set_name("s"); })},
      {"WatchedDirectoryBody", true, modRt([](auto& r) {
         r.clear_route();
         r.mutable_direct_response()->mutable_body()->mutable_watched_directory()->set_path("/tmp");
       })},
      {"UnvettedPerFilterAny", true, vhostWithFilter(makeTypeAny(kDmType))},
      {"EmptyPerFilterAnyTypeUrl", true, vhostWithFilter(Protobuf::Any{})},
      {"UnlinkedVettedAnyType", true, vhostWithFilter(makeTypeAny(kUnlinkedVettedType))},
      {"CorruptSerializedVettedAny", true,
       vhostWithFilter(makeTypeAny(kBufferType, "\xff\xff\xff"))},
      {"DisabledFilterConfigUnvetted", false,
       vhostWithFilter(makeFilterConfigAny(true, makeTypeAny(kDmType)))},
      {"FilterConfigNoInnerField", false, vhostWithFilter(makeFilterConfigAny(false))},
      {"FilterConfigEmptyInnerAny", false, vhostWithFilter(makeAny(empty_inner_fc))},
      {"EnabledFilterConfigVetted", false,
       vhostWithFilter(makeFilterConfigAny(false, makeBufferAny(4096)))},
      {"EnabledFilterConfigUnvetted", true,
       vhostWithFilter(makeFilterConfigAny(false, makeTypeAny(kDmType)))},
      {"XdsTypedStructVettedBuffer", true, routeWithFilter(makeAny(xds_ts))},
      {"UdpaTypedStructOnWeightedCluster", true, modAct([&](auto& a) {
         auto* wc = a.mutable_weighted_clusters()->add_clusters();
         wc->set_name("cluster_1");
         (*wc->mutable_typed_per_filter_config())["dm"] = makeAny(udpa_fc);
       })},
      {"OnDemandCdsPerRoute", true, routeWithFilter(makeAny(od_cfg))},
      {"LocalRateLimitPerRoute", true, routeWithFilter(makeAny(local_rl))},
      {"StatefulSessionHeader", false, routeWithFilter(makeSessionAny(makeAny(hdr_session)))},
      {"StatefulSessionCookie", true,
       routeWithFilter(makeSessionAny(makeTypeAny(
           "envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState")))},
      {"MisleadingLabelVettedFormatter", false,
       routeWithFormatter("user_label_with_dynamic_modules_in_name", makeAny(req_wo_query))},
      {"HarmlessLabelSecretFormatter", true,
       routeWithFormatter("harmless_label", makeTypeAny(kGenericSecretType))},
      {"FileContentFormatter", true,
       routeWithFormatter("fc",
                          makeTypeAny("envoy.extensions.formatter.file_content.v3.FileContent"))},
      {"ClusterSpecifierNoTypedConfig", true, modAct([](auto& a) {
         a.mutable_inline_cluster_specifier_plugin()->mutable_extension()->set_name("missing");
       })},
      {"RetryPredicateNoTypedConfig", true, modVh([](auto& v) {
         v.mutable_retry_policy()->add_retry_host_predicate()->set_name("unvetted.predicate");
       })},
      {"RetryPriorityNoTypedConfig", true, modAct([](auto& a) {
         a.mutable_retry_policy()->mutable_retry_priority()->set_name("unvetted");
       })},
      {"PreviousHostsRetryPredicate", false, modVh([](auto& v) {
         envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate prev;
         setExtension(v.mutable_retry_policy()->add_retry_host_predicate(), "prev", makeAny(prev));
       })},
      {"CustomLuaHeaderStringMatcher", true, modRt([](auto& r) {
         setExtension(r.mutable_match()->add_headers()->mutable_string_match()->mutable_custom(),
                      "lua_sm", makeTypeAny("envoy.extensions.string_matcher.lua.v3.Lua"));
       })},
      {"UriTemplateMatchAndRewrite", false, modRt([](auto& r) {
         envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig mc;
         mc.set_path_template("/api/{version}/items");
         envoy::extensions::path::rewrite::uri_template::v3::UriTemplateRewriteConfig rc;
         rc.set_path_template_rewrite("/v2/{version}/items");
         setExtension(r.mutable_match()->mutable_path_match_policy(), "ut_m", makeAny(mc));
         setExtension(r.mutable_route()->mutable_path_rewrite_policy(), "ut_r", makeAny(rc));
       })},
      {"RateLimitVettedHeaderInput", false, modVh([&](auto& v) {
         setExtension(v.add_rate_limits()->add_actions()->mutable_extension(), "hdr",
                      makeAny(req_hdr_input));
       })},
      {"RateLimitUnvettedExpr", true, modVh([](auto& v) {
         setExtension(v.add_rate_limits()->add_actions()->mutable_extension(), "expr",
                      makeTypeAny("envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor"));
       })},
      {"SetFilterStateVettedFormatter", false, routeWithFilter(makeSfsAny(makeAny(req_wo_query)))},
      {"SetFilterStateSecretFormatter", true,
       routeWithFilter(makeSfsAny(makeTypeAny(kGenericSecretType)))},
      {"DeepJsonMetadataStruct30Levels", false, modVh([](auto& v) {
         Protobuf::Struct* s = &(*v.mutable_metadata()->mutable_filter_metadata())["envoy.lb"];
         for (int i = 0; i < 30; ++i) {
           s = (*s->mutable_fields())["nested"].mutable_struct_value();
         }
         (*s->mutable_fields())["leaf"].set_string_value("val");
       })},
      {"RawStructPackedInAny", true, vhostWithFilter(makeAny(Protobuf::Struct{}))},
      {"EmptyMatcher", false, modVh([](auto& v) {
         v.clear_routes();
         v.mutable_matcher();
       })},
      {"NestedBooleanMatcherVetted", false, makeMatcherListVhost("request-headers", true)},
      {"MatcherUriTemplateInputMatcher", true,
       makeMatcherListVhost(
           "request-headers", false,
           makeTypeAny("envoy.extensions.matching.input_matchers.uri_template.v3.UriTemplate"))},
      {"MatcherTreeDynamicModuleInput", true, modVh([](auto& v) {
         v.clear_routes();
         setExtension(
             v.mutable_matcher()->mutable_matcher_tree()->mutable_input(), "dm",
             makeTypeAny(
                 "envoy.extensions.matching.http.dynamic_modules.v3.DynamicModuleDataInput"));
       })},
      {"MatcherTreePrefixMapRouteList", false, makeTreeMapVhost(false)},
      {"MatcherTreeRouteListSpecifiers", true, makeTreeMapVhost(true)},
      {"MatcherTreeCustomMatchNoConfig", true, modVh([](auto& v) {
         v.clear_routes();
         v.mutable_matcher()->mutable_matcher_tree()->mutable_custom_match()->set_name("cm");
       })},
      {"MatcherTreeCustomMatchUnvetted", true, modVh([](auto& v) {
         v.clear_routes();
         setExtension(v.mutable_matcher()->mutable_matcher_tree()->mutable_custom_match(), "cm",
                      makeTypeAny("google.protobuf.StringValue"));
       })},
      {"MatcherOnNoMatchCustomAction", true, modVh([](auto& v) {
         v.clear_routes();
         setExtension(v.mutable_matcher()->mutable_on_no_match()->mutable_action(), "ca",
                      makeTypeAny("google.protobuf.StringValue"));
       })},
      {"FilterConfigMaxDepthExceeded", true, vhostWithFilter(std::move(deep_fc))},
  };

  for (const auto& tc : cases) {
    SCOPED_TRACE(tc.name);
    const bool expected = kFullProtosEnabled ? tc.eager_when_full_protos : true;
    EXPECT_EQ(expected, requiresMainThreadInit(tc.vhost));
  }

#if defined(ENVOY_ENABLE_FULL_PROTOS)
  const auto dm_label_rc = makeRouteConfig(
      {makeMatcherListVhost("user_label_with_dynamic_modules_and_wasm_and_lua", false)});
  auto dm_label_matcher = createMatcher(dm_label_rc, false);
  ASSERT_NE(dm_label_matcher, nullptr);
  EXPECT_TRUE(dm_label_matcher->findDomainEntry("dmlabel.example.com")->isDeferred());
#endif
}

TEST_F(DeferredVirtualHostTest, RoutingPrecedenceVhostHeaderAndLiteFallback) {
  factory_context_.cluster_manager_.initializeClusters(
      {"c_exact", "c_long_suf", "c_short_suf", "c_long_pre", "c_short_pre", "c_default"}, {});

  // 1. Empty virtual_hosts and default-only ("*") fast paths.
  auto empty_matcher = createMatcher(RouteConfigProto{});
  ASSERT_NE(empty_matcher, nullptr);
  EXPECT_EQ(empty_matcher->findDomainEntry("any.example.com"), nullptr);
  EXPECT_EQ(empty_matcher->findVirtualHost(genHeaders("any.example.com", "/")), nullptr);

  const auto default_only_rc = makeRouteConfig({makeBaseVhost("default_only", "*", "c_default")});
  auto default_matcher = createMatcher(default_only_rc);
  ASSERT_NE(default_matcher, nullptr);
  EXPECT_EQ(kFullProtosEnabled, default_matcher->findDomainEntry("any.example.com")->isDeferred());
  auto default_cfg = createConfig(default_only_rc);
  ASSERT_NE(default_cfg, nullptr);
  EXPECT_TRUE(default_cfg->virtualHostExists("any.example.com"));
  ASSERT_NE(default_cfg->route(genHeaders("any.example.com", "/"), stream_info_, 0).route, nullptr);

  // 2. Disabled vs enabled deferral and domain/wildcard precedence.
  auto rc = makeRouteConfig({
      makeBaseVhost("vhost_default", "*", "c_default"),
      makeBaseVhost("vhost_short_prefix", "service.*", "c_short_pre"),
      makeBaseVhost("vhost_long_prefix", "service.sub.*", "c_long_pre"),
      makeBaseVhost("vhost_short_suffix", "*.example.com", "c_short_suf"),
      makeBaseVhost("vhost_long_suffix", "*.sub.example.com", "c_long_suf"),
      makeBaseVhost("vhost_exact", "service.sub.example.com", "c_exact"),
  });
  rc.set_ignore_port_in_host_matching(true);

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

  factory_context_.bootstrap_.mutable_route_manager()->set_enable_deferred_virtual_host_creation(
      true);
  auto matcher = createMatcher(rc);
  ASSERT_NE(matcher, nullptr);
  const DomainEntry* exact_entry = matcher->findDomainEntry("SERVICE.SUB.EXAMPLE.COM:8443");
  ASSERT_NE(exact_entry, nullptr);
  EXPECT_EQ(kFullProtosEnabled, exact_entry->isDeferred());
  EXPECT_EQ(kFullProtosEnabled, exact_entry->activeVirtualHost() == nullptr);
  ASSERT_NE(matcher->findVirtualHost("SERVICE.SUB.EXAMPLE.COM:8443"), nullptr);
  EXPECT_NE(exact_entry->activeVirtualHost(), nullptr);

  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  EXPECT_TRUE(config->virtualHostExists("service.sub.example.com:8080"));
  EXPECT_TRUE(config->virtualHostExists("unmatched.domain"));

  const std::pair<absl::string_view, absl::string_view> precedence_cases[] = {
      {"SERVICE.SUB.EXAMPLE.COM:8443", "vhost_exact"},
      {"foo.sub.example.com", "vhost_long_suffix"},
      {"foo.example.com", "vhost_short_suffix"},
      {"service.sub.org", "vhost_long_prefix"},
      {"service.net", "vhost_short_prefix"},
      {".example.com", "vhost_default"},
      {"example.com", "vhost_default"},
      {"unmatched.other.org", "vhost_default"},
  };
  for (const auto& [host, expected_vhost] : precedence_cases) {
    SCOPED_TRACE(host);
    const auto res = config->route(genHeaders(host, "/"), stream_info_, 0);
    ASSERT_NE(res.route, nullptr);
    EXPECT_EQ(expected_vhost, res.route->virtualHost().name());
  }
  EXPECT_EQ(&config->route(genHeaders("SERVICE.SUB.EXAMPLE.COM:8443", "/"), stream_info_, 0)
                 .route->virtualHost(),
            &config->route(genHeaders("service.sub.example.com", "/"), stream_info_, 0)
                 .route->virtualHost());

  // 3. vhost_header and missing :authority semantics.
  auto vhost_hdr_rc = makeRouteConfig({
      makeBaseVhost("vhost_1", "alt.example.com", "c_exact"),
      makeBaseVhost("vhost_2", "host.example.com", "c_exact"),
  });
  vhost_hdr_rc.set_vhost_header("x-vhost");
  auto vhost_hdr_cfg = createConfig(vhost_hdr_rc);
  ASSERT_NE(vhost_hdr_cfg, nullptr);
  EXPECT_FALSE(vhost_hdr_cfg->virtualHostExists("nonexistent.example.com"));
  EXPECT_TRUE(vhost_hdr_cfg->virtualHostExists("alt.example.com"));

  auto hdrs = genHeaders("host.example.com", "/");
  EXPECT_EQ(vhost_hdr_cfg->route(hdrs, stream_info_, 0).route, nullptr);
  hdrs.addCopy("x-vhost", "alt.example.com");
  const auto matched = vhost_hdr_cfg->route(hdrs, stream_info_, 0);
  ASSERT_NE(matched.route, nullptr);
  EXPECT_EQ("vhost_1", matched.route->virtualHost().name());

  auto non_default_cfg =
      createConfig(makeRouteConfig({makeBaseVhost("vhost_only", "only.example.com", "c_exact")}));
  ASSERT_NE(non_default_cfg, nullptr);
  Http::TestRequestHeaderMapImpl no_authority{
      {":path", "/"}, {":method", "GET"}, {":scheme", "http"}};
  EXPECT_EQ(non_default_cfg->route(no_authority, stream_info_, 0).route, nullptr);
}

TEST_F(DeferredVirtualHostTest, DirectResponseWatchedVsStaticWorkerFormatting) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("dynamic_body.txt", "dynamic content");

  // 1. watched_directory short-circuits to eager main-thread init without opening throwaway probe
  // watchers.
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(factory_context_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _)).WillOnce(Return(absl::OkStatus()));
  RouteConfigProto watched_rc;
  auto* wvh = watched_rc.add_virtual_hosts();
  wvh->set_name("watched_vhost");
  wvh->add_domains("watched.example.com");
  auto* wbody = addDirectRoute(*wvh, "/", false)->mutable_body();
  wbody->set_filename(file_path);
  wbody->mutable_watched_directory()->set_path(TestEnvironment::temporaryDirectory());
  EXPECT_TRUE(requiresMainThreadInit(*wvh));
  EXPECT_TRUE(requiresProbeValidation(*wvh, nullptr, false, factory_context_));
  auto watched_matcher = createMatcher(watched_rc, false);
  ASSERT_NE(watched_matcher, nullptr);
  EXPECT_FALSE(watched_matcher->findDomainEntry("watched.example.com")->isDeferred());

  // 2. Static direct_response (inline_string, static filename, and body_format) stays deferred
  // without touching mainThreadDispatcher() or threadLocal(), and formats on a worker thread.
  EXPECT_CALL(factory_context_, mainThreadDispatcher()).Times(0);
  EXPECT_CALL(factory_context_, threadLocal()).Times(0);
  RouteConfigProto static_rc;
  auto* svh = static_rc.add_virtual_hosts();
  svh->set_name("static_direct_vhost");
  svh->add_domains("direct.example.com");
  addDirectRoute(*svh, "/inline", true)->mutable_body()->set_inline_string("inline payload");
  auto* fmt_dr = addDirectRoute(*svh, "/formatted", true);
  fmt_dr->mutable_body()->set_inline_string("base-body");
  fmt_dr->mutable_body_format()->mutable_text_format_source()->set_inline_string(
      "%LOCAL_REPLY_BODY%:%REQ(x-tag)%");
  addDirectRoute(*svh, "/", false)->mutable_body()->set_filename(file_path);

  EXPECT_EQ(!kFullProtosEnabled, requiresMainThreadInit(*svh));
  auto static_matcher = createMatcher(static_rc, false);
  ASSERT_NE(static_matcher, nullptr);
  EXPECT_EQ(kFullProtosEnabled,
            static_matcher->findDomainEntry("direct.example.com")->isDeferred());

  auto static_config = createConfig(static_rc, false);
  ASSERT_NE(static_config, nullptr);
  std::thread worker([&static_config]() {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
    Http::TestResponseHeaderMapImpl resp_headers;
    std::string body_out;
    for (const auto& [path, expected_body] :
         {std::pair{"/inline", "inline payload"}, std::pair{"/formatted", "base-body:ok"},
          std::pair{"/file", "dynamic content"}}) {
      auto hdrs = genHeaders("direct.example.com", path);
      hdrs.addCopy("x-tag", "ok");
      auto r = static_config->route(hdrs, local_info, 0);
      ASSERT_NE(r.route, nullptr);
      ASSERT_NE(r.route->directResponseEntry(), nullptr);
      EXPECT_EQ(expected_body, r.route->directResponseEntry()->formatBody(hdrs, resp_headers,
                                                                          local_info, body_out));
    }
  });
  worker.join();
}

TEST_F(DeferredVirtualHostTest, FastPathRequiresProbeValidationTable) {
  factory_context_.cluster_manager_.initializeClusters({"cluster_1", "shadow_cluster"}, {});

  const RouteConfigProto base_rc =
      makeRouteConfig({makeBaseVhost("prefix_vhost", "prefix.example.com", "cluster_1")});

  VHostProto full_fast_vhost = makeBaseVhost("exact_and_sep_vhost", "exact.example.com");
  full_fast_vhost.set_require_tls(VHostProto::EXTERNAL_ONLY);
  auto* r1 = full_fast_vhost.mutable_routes(0);
  r1->mutable_match()->set_path("/v1/item");
  r1->mutable_match()->mutable_case_sensitive()->set_value(false);
  r1->mutable_route()->set_priority(envoy::config::core::v3::HIGH);
  r1->mutable_route()->set_cluster_not_found_response_code(ActionProto::NOT_FOUND);
  auto* r2 = full_fast_vhost.add_routes();
  r2->mutable_match()->set_path_separated_prefix("/service");
  auto* a2 = r2->mutable_route();
  a2->set_cluster("cluster_1");
  for (auto* d : {a2->mutable_timeout(), a2->mutable_idle_timeout(), a2->mutable_flush_timeout(),
                  a2->mutable_max_grpc_timeout(), a2->mutable_grpc_timeout_offset(),
                  a2->mutable_max_stream_duration()->mutable_max_stream_duration(),
                  a2->mutable_max_stream_duration()->mutable_grpc_timeout_header_max(),
                  a2->mutable_max_stream_duration()->mutable_grpc_timeout_header_offset()}) {
    d->set_seconds(10);
  }

  RouteConfigProto shadow_fast_rc =
      makeRouteConfig({makeBaseVhost("shadow_vhost", "shadow.example.com", "cluster_1")});
  shadow_fast_rc.add_request_mirror_policies()->set_cluster("shadow_cluster");
  shadow_fast_rc.add_request_mirror_policies()->set_cluster_header("x-shadow-cluster");

  for (const auto& rc : {base_rc, makeRouteConfig({full_fast_vhost}), shadow_fast_rc}) {
    auto global_or_error = CommonConfigImpl::create(
        rc, factory_context_, ProtobufMessage::getNullValidationVisitor(), init_manager_);
    ASSERT_OK(global_or_error.status());
    EXPECT_EQ(!kFullProtosEnabled,
              requiresProbeValidation(rc.virtual_hosts(0), global_or_error.value(), true,
                                      factory_context_));
    ASSERT_NE(createConfig(rc), nullptr);
  }

  auto global_cfg =
      CommonConfigImpl::create(base_rc, factory_context_,
                               ProtobufMessage::getNullValidationVisitor(), init_manager_)
          .value();
  const auto& base_vhost = base_rc.virtual_hosts(0);
  EXPECT_TRUE(requiresProbeValidation(base_vhost, nullptr, true, factory_context_));

  // Unknown global shadow cluster & unknown route cluster depend on validate_clusters.
  RouteConfigProto bad_shadow_rc = base_rc;
  bad_shadow_rc.add_request_mirror_policies()->set_cluster("unknown_shadow");
  auto bad_shadow_global =
      CommonConfigImpl::create(bad_shadow_rc, factory_context_,
                               ProtobufMessage::getNullValidationVisitor(), init_manager_)
          .value();
  EXPECT_TRUE(requiresProbeValidation(base_vhost, bad_shadow_global, true, factory_context_));
  EXPECT_EQ(!kFullProtosEnabled,
            requiresProbeValidation(base_vhost, bad_shadow_global, false, factory_context_));

  const auto unknown_cluster_vhost = modAct([](auto& a) { a.set_cluster("unknown_cluster"); });
  EXPECT_TRUE(requiresProbeValidation(unknown_cluster_vhost, global_cfg, true, factory_context_));
  EXPECT_EQ(!kFullProtosEnabled,
            requiresProbeValidation(unknown_cluster_vhost, global_cfg, false, factory_context_));

#if defined(ENVOY_ENABLE_FULL_PROTOS)
  {
    auto unknown_vhost = base_vhost;
    unknown_vhost.GetReflection()->MutableUnknownFields(&unknown_vhost)->AddVarint(999, 1);
    EXPECT_TRUE(requiresProbeValidation(unknown_vhost, global_cfg, true, factory_context_));

    auto unknown_msd_vhost = base_vhost;
    auto* u_msd =
        unknown_msd_vhost.mutable_routes(0)->mutable_route()->mutable_max_stream_duration();
    u_msd->GetReflection()->MutableUnknownFields(u_msd)->AddVarint(888, 1);
    EXPECT_TRUE(requiresProbeValidation(unknown_msd_vhost, global_cfg, true, factory_context_));
  }
#endif

  const VHostProto probe_required_vhosts[] = {
      modVh([](auto& v) { v.mutable_matcher(); }),
      modVh([](auto& v) { v.add_virtual_clusters()->set_name("vc"); }),
      modVh([](auto& v) { v.add_rate_limits(); }),
      vhostWithFilter(makeTypeAny("google.protobuf.Struct")),
      modVh([](auto& v) { v.add_request_mirror_policies()->set_cluster("cluster_1"); }),
      modVh([](auto& v) { v.mutable_retry_policy(); }),
      modVh([](auto& v) { v.mutable_hedge_policy(); }),
      modVh([](auto& v) { v.mutable_metadata(); }),
      modVh([](auto& v) { v.add_request_headers_to_add(); }),
      modVh([](auto& v) { v.add_request_headers_to_remove("x-remove"); }),
      modVh([](auto& v) { v.add_response_headers_to_add(); }),
      modVh([](auto& v) { v.add_response_headers_to_remove("x-remove"); }),
      modVh([](auto& v) { v.set_require_tls(static_cast<VHostProto::TlsRequirementType>(-1)); }),
      modRt([](auto& r) { r.mutable_redirect()->set_host_redirect("other.com"); }),
      modRt([](auto& r) { r.mutable_direct_response()->set_status(200); }),
      modRt([](auto& r) { r.clear_action(); }),
      modRt([](auto& r) { r.add_request_headers_to_add(); }),
      modRt([](auto& r) { r.mutable_metadata(); }),
      modRt([](auto& r) { r.mutable_decorator()->set_operation("op"); }),
      modRt([](auto& r) { r.mutable_tracing(); }),
      modRt([](auto& r) { r.mutable_match()->mutable_safe_regex()->set_regex(".*"); }),
      modRt([](auto& r) { r.mutable_match()->mutable_connect_matcher(); }),
      modRt([](auto& r) { r.mutable_match()->clear_path_specifier(); }),
      modRt([](auto& r) { r.mutable_match()->add_headers()->set_name("hdr"); }),
      modRt([](auto& r) { r.mutable_match()->add_query_parameters()->set_name("q"); }),
      modAct([](auto& a) { a.clear_cluster(); }),
      modAct([](auto& a) { a.set_cluster(""); }),
      modAct([](auto& a) { a.mutable_weighted_clusters()->add_clusters()->set_name("cluster_1"); }),
      modAct([](auto& a) { a.set_cluster_header("x-cluster"); }),
      modAct([](auto& a) {
        a.set_cluster_not_found_response_code(
            static_cast<ActionProto::ClusterNotFoundResponseCode>(-1));
      }),
      modAct([](auto& a) {
        a.set_priority(static_cast<envoy::config::core::v3::RoutingPriority>(-1));
      }),
      modAct(
          [](auto& a) { a.set_internal_redirect_action(ActionProto::HANDLE_INTERNAL_REDIRECT); }),
      modAct([](auto& a) { a.mutable_timeout()->set_seconds(-1); }),
      modAct([](auto& a) { a.mutable_idle_timeout()->set_seconds(-1); }),
      modAct([](auto& a) { a.mutable_flush_timeout()->set_seconds(-1); }),
      modAct([](auto& a) { a.mutable_max_grpc_timeout()->set_seconds(-1); }),
      modAct([](auto& a) { a.mutable_grpc_timeout_offset()->set_seconds(-1); }),
      modAct([](auto& a) {
        a.mutable_max_stream_duration()->mutable_max_stream_duration()->set_seconds(-1);
      }),
      modAct([](auto& a) {
        a.mutable_max_stream_duration()->mutable_grpc_timeout_header_max()->set_seconds(-1);
      }),
      modAct([](auto& a) {
        a.mutable_max_stream_duration()->mutable_grpc_timeout_header_offset()->set_seconds(-1);
      }),
      modAct([](auto& a) { a.set_prefix_rewrite("/new"); }),
      modAct([](auto& a) { a.mutable_internal_redirect_policy(); }),
  };
  for (const auto& v : probe_required_vhosts) {
    EXPECT_TRUE(requiresProbeValidation(v, global_cfg, true, factory_context_));
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

  template <typename ProtoT>
  class RecordingFilterFactory : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    RecordingFilterFactory(const std::string& filter_name, const std::string& proto_type)
        : EmptyHttpFilterConfig(filter_name), proto_type_(proto_type) {}

    absl::StatusOr<Http::FilterFactoryCb>
    createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
      PANIC("not implemented");
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return std::make_unique<ProtoT>();
    }
    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<ProtoT>();
    }
    std::set<std::string> configTypes() override { return {proto_type_}; }

    absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr> createHttpFilterRouteConfig(
        const Protobuf::Message&, Server::Configuration::ServerFactoryContext& context,
        Server::Configuration::ExtraFactoryContext& extra_context) override {
      init_managers_.push_back(extra_context.init_manager.ptr());
      if (hook_) {
        hook_(context, extra_context);
      }
      return std::make_shared<ContextCapturingRouteConfig>(context);
    }

    void reset() {
      init_managers_.clear();
      hook_ = nullptr;
    }

    const std::string proto_type_;
    std::vector<Init::Manager*> init_managers_;
    std::function<void(Server::Configuration::ServerFactoryContext&,
                       Server::Configuration::ExtraFactoryContext&)>
        hook_;
  };

  RecordingFilterFactory<envoy::extensions::filters::http::buffer::v3::BufferPerRoute> factory_{
      "test.deferred.filter", "envoy.extensions.filters.http.buffer.v3.BufferPerRoute"};
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_{
      factory_};
  RecordingFilterFactory<Protobuf::Timestamp> unvetted_factory_{"test.unvetted.filter",
                                                                "google.protobuf.Timestamp"};
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_unvetted_{
      unvetted_factory_};
};

TEST_F(ProbingFilterTest, IsolatedValidationContextIsolatesAndDetectsMainThreadResources) {
  factory_context_.cluster_manager_.initializeClusters({"baz"}, {});

  // 0. Direct IsolatedValidationContext and ScopedValidationServerContext verification (runs in
  // both full-proto and lite-proto builds).
  {
    ScopedThreadLocalServerContextSetter server_context_setter(factory_context_);
    IsolatedValidationContext iso_ctx(factory_context_);
    EXPECT_FALSE(iso_ctx.requiresMainThreadInit());
    exerciseNoOpInitManager(iso_ctx.initManager(), Init::Manager::State::Uninitialized);
    {
      ScopedValidationServerContext scoped_server_ctx(iso_ctx);
      EXPECT_FALSE(iso_ctx.requiresMainThreadInit());
      std::ignore = Server::Configuration::ServerFactoryContextInstance::get().runtime();
      EXPECT_TRUE(iso_ctx.requiresMainThreadInit());
    }
    EXPECT_EQ(Server::Configuration::ServerFactoryContextInstance::getExisting(),
              &factory_context_);
  }

#if defined(ENVOY_ENABLE_FULL_PROTOS)
  RouteConfigProto rc;
  (*rc.mutable_typed_per_filter_config())["test.deferred.filter"] = makeBufferAny(1024);
  auto* vh = rc.add_virtual_hosts();
  vh->set_name("bar");
  vh->add_domains("bar.example.com");
  (*vh->mutable_typed_per_filter_config())["test.deferred.filter"] = makeBufferAny(2048);
  auto* rt = vh->add_routes();
  rt->mutable_match()->set_prefix("/");
  (*rt->mutable_typed_per_filter_config())["test.deferred.filter"] = makeBufferAny(4096);
  auto* wc = rt->mutable_route()->mutable_weighted_clusters()->add_clusters();
  wc->set_name("baz");
  wc->mutable_weight()->set_value(100);
  (*wc->mutable_typed_per_filter_config())["test.deferred.filter"] = makeBufferAny(8192);

  // 1. Pure worker-safe filter config: isolated probe validation runs once, virtual host stays
  // deferred after transient RDS Init::Manager is destroyed, and inflates on a worker thread with
  // WorkerSafeFactoryContext whose lifetime outlives ConfigImpl via RouteConstSharedPtr.
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
    }
    ASSERT_NE(retained_route, nullptr);
    const auto* captured = retained_route->mostSpecificPerFilterConfig("test.deferred.filter");
    ASSERT_NE(captured, nullptr);
    const auto* typed_captured = dynamic_cast<const ContextCapturingRouteConfig*>(captured);
    ASSERT_NE(typed_captured, nullptr);
    auto& worker_init_mgr = typed_captured->context.initManager();
    EXPECT_NE(&worker_init_mgr, &factory_context_.initManager());
    Init::TargetImpl dummy_target("noop", []() {});
    worker_init_mgr.add(dummy_target);
    exerciseNoOpInitManager(worker_init_mgr, Init::Manager::State::Initialized);
    EXPECT_EQ(&typed_captured->context.messageValidationVisitor(),
              &ProtobufMessage::getNullValidationVisitor());
    EXPECT_EQ(&typed_captured->context.runtime(), &factory_context_.runtime());
  }

  VHostProto single_filter_vh = routeWithFilter(makeBufferAny(1024), "test.deferred.filter");
  single_filter_vh.set_name("bar");
  single_filter_vh.set_domains(0, "bar.example.com");
  single_filter_vh.mutable_routes(0)->mutable_route()->set_cluster("baz");
  const auto single_filter_rc = makeRouteConfig({single_filter_vh});

  auto expectProbeForcesEager = [&](std::function<void(Server::Configuration::ServerFactoryContext&,
                                                       Server::Configuration::ExtraFactoryContext&)>
                                        hook) {
    factory_.reset();
    factory_.hook_ = std::move(hook);
    auto matcher = createMatcher(single_filter_rc);
    factory_.hook_ = nullptr;
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.init_managers_.size(), 2);
    EXPECT_NE(factory_.init_managers_[0], &init_manager_);
    EXPECT_EQ(factory_.init_managers_[1], &init_manager_);
  };

  // 2. Init::Target registration during probe forces eager main-thread initialization.
  {
    Init::TargetImpl target("test_target", []() {});
    EXPECT_CALL(init_manager_, add(_));
    expectProbeForcesEager([&](auto&, auto& extra) {
      if (extra.init_manager.has_value()) {
        extra.init_manager->add(target);
      }
    });
  }

  // 3. ThreadLocal slot allocation uses isolated NoOpSlot and forces eager main-thread init.
  {
    std::vector<ThreadLocal::TypedSlotPtr<>> slots;
    EXPECT_CALL(factory_context_.thread_local_, allocateSlot()); // Only eager fallback!
    expectProbeForcesEager([&](Server::Configuration::ServerFactoryContext& ctx, auto&) {
      auto slot = ThreadLocal::TypedSlot<>::makeUnique(ctx.threadLocal());
      slot->set(
          [](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return nullptr; });
      slot->runOnAllThreads([](OptRef<ThreadLocal::ThreadLocalObject>) {});
      if (slots.empty()) {
        slot->runOnAllThreads([](OptRef<ThreadLocal::ThreadLocalObject>) {}, nullptr);
      }
      bool completed = false;
      slot->runOnAllThreads([](OptRef<ThreadLocal::ThreadLocalObject>) {},
                            [&completed]() { completed = true; });
      EXPECT_TRUE(completed);
      slots.push_back(std::move(slot));
    });
    ASSERT_EQ(slots.size(), 2);
    EXPECT_FALSE(slots[0]->currentThreadRegistered());
    EXPECT_FALSE(slots[0]->get().has_value());
    EXPECT_FALSE(slots[0]->isShutdown());
  }

  // 4. Singleton creation during probe stays isolated in local_manager_ (even when pin=true),
  // while non-constructing lookups still see pre-existing parent singletons.
  {
    auto parent_singleton = factory_context_.singletonManager().getTyped<TestSingletonInstance>(
        SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_parent_singleton),
        []() { return std::make_shared<TestSingletonInstance>(); }, /*pin=*/true);
    ASSERT_NE(parent_singleton, nullptr);

    std::vector<std::shared_ptr<TestSingletonInstance>> created;
    std::shared_ptr<TestSingletonInstance> observed_parent;
    expectProbeForcesEager([&](Server::Configuration::ServerFactoryContext& ctx, auto&) {
      created.push_back(ctx.singletonManager().getTyped<TestSingletonInstance>(
          SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_test_singleton),
          []() { return std::make_shared<TestSingletonInstance>(); }, /*pin=*/true));
      observed_parent = ctx.singletonManager().getTyped<TestSingletonInstance>(
          SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_parent_singleton));
    });
    ASSERT_EQ(created.size(), 2);
    EXPECT_NE(created[0], created[1]);
    EXPECT_EQ(factory_context_.singletonManager().getTyped<TestSingletonInstance>(
                  SINGLETON_MANAGER_REGISTERED_NAME(deferred_vhost_test_singleton)),
              created[1]);
    EXPECT_EQ(observed_parent, parent_singleton);
  }

  // 5. All main-thread-bound primitives on IsolatedValidationContext force eager main-thread init.
  const std::pair<absl::string_view,
                  std::function<void(Server::Configuration::ServerFactoryContext&)>>
      main_thread_hooks[] = {
          {"SecretManager", [](auto& ctx) { std::ignore = ctx.secretManager(); }},
          {"ClusterManager", [](auto& ctx) { std::ignore = ctx.clusterManager(); }},
          {"XdsManager", [](auto& ctx) { std::ignore = ctx.xdsManager(); }},
          {"MainThreadDispatcher", [](auto& ctx) { std::ignore = ctx.mainThreadDispatcher(); }},
          {"Admin", [](auto& ctx) { std::ignore = ctx.admin(); }},
          {"AccessLogManager", [](auto& ctx) { std::ignore = ctx.accessLogManager(); }},
          {"DrainManager", [](auto& ctx) { std::ignore = ctx.drainManager(); }},
          {"LifecycleNotifier", [](auto& ctx) { std::ignore = ctx.lifecycleNotifier(); }},
          {"SslContextManager", [](auto& ctx) { std::ignore = ctx.sslContextManager(); }},
      };
  for (const auto& [name, fn] : main_thread_hooks) {
    SCOPED_TRACE(name);
    expectProbeForcesEager([&](auto& ctx, auto&) { fn(ctx); });
  }

  // 5b. Worker-safe DelegatingServerFactoryContext accessors keep the virtual host deferred.
  {
    factory_.reset();
    factory_.hook_ = [&](Server::Configuration::ServerFactoryContext& ctx, auto&) {
      EXPECT_EQ(&ctx.scope(), &ctx.serverScope());
      EXPECT_FALSE(ctx.healthCheckFailed());
      EXPECT_FALSE(ctx.threadLocal().isShutdown());
      std::ignore = std::make_tuple(
          &ctx.httpServerPropertiesCacheManager(), &ctx.options(), &ctx.localInfo(),
          &ctx.messageValidationContext(), &ctx.runtime(), &ctx.timeSource(), &ctx.api(),
          &ctx.httpContext(), &ctx.grpcContext(), &ctx.routerContext(), ctx.processContext(),
          &ctx.regexEngine(), &ctx.statsConfig(), &ctx.bootstrap(), &ctx.overloadManager(),
          &ctx.nullOverloadManager(), &ctx.threadLocal().dispatcher());
    };
    auto matcher = createMatcher(single_filter_rc);
    factory_.hook_ = nullptr;
    ASSERT_NE(matcher, nullptr);
    EXPECT_TRUE(matcher->findDomainEntry("bar.example.com")->isDeferred());
    ASSERT_EQ(factory_.init_managers_.size(), 1);
  }

  // 5c. Unvetted Any type URLs skip probe_vhost completely and initialize eagerly once.
  {
    VHostProto unvetted_vh =
        routeWithFilter(makeAny(Protobuf::Timestamp{}), "test.unvetted.filter");
    unvetted_vh.set_domains(0, "unvetted.example.com");
    unvetted_vh.mutable_routes(0)->mutable_route()->set_cluster("baz");
    unvetted_factory_.reset();
    auto matcher = createMatcher(makeRouteConfig({unvetted_vh}));
    ASSERT_NE(matcher, nullptr);
    EXPECT_FALSE(matcher->findDomainEntry("unvetted.example.com")->isDeferred());
    ASSERT_EQ(unvetted_factory_.init_managers_.size(), 1);
    EXPECT_EQ(unvetted_factory_.init_managers_[0], &init_manager_);
  }

  // 6. Concurrent worker-thread inflation of matcher-based virtual hosts never touches an
  // unsynchronized parent WarningValidationVisitor off-thread.
  {
    class MainThreadOnlyWarningVisitor : public ProtobufMessage::WarningValidationVisitorImpl {
    public:
      const std::thread::id main_id{std::this_thread::get_id()};
      std::atomic<int> off_thread_calls{0};
      void checkThread() {
        if (std::this_thread::get_id() != main_id) {
          off_thread_calls.fetch_add(1, std::memory_order_relaxed);
        }
      }
      bool skipValidation() override {
        checkThread();
        return WarningValidationVisitorImpl::skipValidation();
      }
      absl::Status onUnknownField(absl::string_view description) override {
        checkThread();
        return WarningValidationVisitorImpl::onUnknownField(description);
      }
    } warning_visitor;
    ON_CALL(factory_context_, messageValidationVisitor()).WillByDefault(ReturnRef(warning_visitor));

    RouteConfigProto matcher_rc;
    constexpr int kNumVhosts = 16;
    for (int i = 0; i < kNumVhosts; ++i) {
      auto* mvh = matcher_rc.add_virtual_hosts();
      mvh->set_name(fmt::format("mv_{}", i));
      mvh->add_domains(fmt::format("m{}.example.com", i));
      RouteProto action_route = makeBaseVhost("tmp", "tmp", "baz").routes(0);
      action_route.GetReflection()->MutableUnknownFields(&action_route)->AddVarint(777 + i, 1);
      setExtension(mvh->mutable_matcher()->mutable_on_no_match()->mutable_action(), "route",
                   makeAny(action_route));
    }

    auto config = createConfig(matcher_rc);
    ASSERT_NE(config, nullptr);
    runConcurrently(8, [&](int t) {
      NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
      for (int i = 0; i < kNumVhosts; ++i) {
        const int idx = (i + t) % kNumVhosts;
        EXPECT_NE(config->route(genHeaders(fmt::format("m{}.example.com", idx), "/"), local_info, 0)
                      .route,
                  nullptr);
      }
    });
    EXPECT_EQ(warning_visitor.off_thread_calls.load(), 0);
    ON_CALL(factory_context_, messageValidationVisitor())
        .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  }
#endif

#if defined(USE_CEL_PARSER)
  // 7. %CEL(...)% formatter accesses ServerFactoryContextInstance::get(), which
  // ScopedValidationServerContext tracks to force eager main-thread initialization while restoring
  // ServerFactoryContextInstance afterwards.
  {
    ScopedThreadLocalServerContextSetter server_context_setter(factory_context_);
    VHostProto cel_vh = makeBaseVhost("cel_vhost", "cel.example.com", "baz");
    addHeaderOption(cel_vh.mutable_request_headers_to_add(), "x-cel",
                    "%CEL(request.headers['x-version'])%");
    auto config = createConfig(makeRouteConfig({cel_vh}));
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

  VHostProto legacy_vh = makeBaseVhost("legacy_headers_vhost", "headers.example.com", "cluster_1");
  addHeaderOption(legacy_vh.mutable_request_headers_to_add(), "x-translated",
                  "%UPSTREAM_METADATA([\"a\", \"b\"])% %PER_REQUEST_STATE(my_state)%");
  auto* mirror = legacy_vh.add_request_mirror_policies();
  mirror->set_cluster("cluster_1");
  auto* mut_hdr = mirror->add_request_headers_mutations()->mutable_append()->mutable_header();
  mut_hdr->set_key("x-vhost-mirror-translated");
  mut_hdr->set_value("%DYNAMIC_METADATA([\"m\", \"n\"])%");
  auto* route = legacy_vh.mutable_routes(0);
  addHeaderOption(route->mutable_request_headers_to_add(), "x-route-translated",
                  "%DYNAMIC_METADATA([\"x\", \"y\"])%");
  route->mutable_route()->clear_cluster();
  auto* wc = route->mutable_route()->mutable_weighted_clusters()->add_clusters();
  wc->set_name("cluster_1");
  wc->mutable_weight()->set_value(100);
  addHeaderOption(wc->mutable_request_headers_to_add(), "x-weighted-translated",
                  "%UPSTREAM_METADATA([\"c\", \"d\"])%");

  RouteConfigProto rc = makeRouteConfig({legacy_vh});
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  auto* mvh = rc.add_virtual_hosts();
  mvh->set_name("legacy_matcher_vhost");
  mvh->add_domains("matcher-headers.example.com");
  RouteProto m_route = makeBaseVhost("tmp", "tmp", "cluster_1").routes(0);
  addHeaderOption(m_route.mutable_request_headers_to_add(), "x-matcher-translated",
                  "%DYNAMIC_METADATA([\"r\", \"1\"])% %PER_REQUEST_STATE(ms)%");
  setExtension(mvh->mutable_matcher()->mutable_on_no_match()->mutable_action(), "route",
               makeAny(m_route));
#endif

  // With thread-local ServerFactoryContextInstance set on main thread, legacy translation touches
  // ServerFactoryContextInstance::getExisting()->runtime() and forces eager main-thread init.
  {
    ScopedThreadLocalServerContextSetter server_context_setter(factory_context_);
    auto eager_matcher = createMatcher(rc);
    ASSERT_NE(eager_matcher, nullptr);
    EXPECT_FALSE(eager_matcher->findDomainEntry("headers.example.com")->isDeferred());
    EXPECT_EQ(Server::Configuration::ServerFactoryContextInstance::getExisting(),
              &factory_context_);
  }

  // Without ServerFactoryContextInstance set, virtual hosts stay deferred (under full protos) and
  // inflate cleanly on a worker thread without assertions.
  auto config = createConfig(rc);
  ASSERT_NE(config, nullptr);
  std::thread worker([&config]() {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> local_info;
    auto r1 = config->route(genHeaders("headers.example.com", "/"), local_info, 0);
    ASSERT_NE(r1.route, nullptr);
    EXPECT_EQ("legacy_headers_vhost", r1.route->virtualHost().name());
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    auto r2 = config->route(genHeaders("matcher-headers.example.com", "/"), local_info, 0);
    ASSERT_NE(r2.route, nullptr);
    EXPECT_EQ("legacy_matcher_vhost", r2.route->virtualHost().name());
#endif
  });
  worker.join();
}

TEST_F(DeferredVirtualHostTest, RejectsInvalidConfigurationsAtIngestionTime) {
  factory_context_.cluster_manager_.initializeClusters(
      {"existing_cluster", "some_cluster", "valid_cluster"}, {});

  auto addDupDomain = [](absl::string_view d1, absl::string_view d2) {
    return [d1, d2](RouteConfigProto& rc, VHostProto& vh, RouteProto&) {
      vh.set_domains(0, std::string(d1));
      *rc.add_virtual_hosts() = makeBaseVhost("v2", d2, "valid_cluster");
    };
  };
  struct InvalidCase {
    absl::string_view name;
    absl::string_view expected_error;
    std::function<void(RouteConfigProto&, VHostProto&, RouteProto&)> mutate;
  };
  const InvalidCase cases[] = {
      {"UnknownCluster", "route: unknown cluster 'missing'",
       [](auto&, auto&, auto& r) { r.mutable_route()->set_cluster("missing"); }},
      {"InvalidRegex", "missing ]",
       [](auto&, auto&, auto& r) { r.mutable_match()->mutable_safe_regex()->set_regex("["); }},
      {"MissingClusterSpecifierPlugin", "Didn't find a registered implementation for 'custom'",
       [](auto&, auto&, auto& r) {
         setExtension(
             r.mutable_route()->mutable_inline_cluster_specifier_plugin()->mutable_extension(),
             "custom", makeTypeAny("google.protobuf.Struct"));
       }},
      {"ConflictingPathRewrite",
       "Specify only one of prefix_rewrite, regex_rewrite or path_rewrite_policy",
       [](auto&, auto&, auto& r) {
         r.mutable_route()->set_prefix_rewrite("/rewritten");
         r.mutable_route()->mutable_regex_rewrite()->mutable_pattern()->set_regex("^/test/(.*)");
         r.mutable_route()->mutable_regex_rewrite()->set_substitution("/\\1");
       }},
      {"NegativeTimeout", "Expected positive duration",
       [](auto&, auto&, auto& r) { r.mutable_route()->mutable_timeout()->set_seconds(-5); }},
      {"MissingGlobalShadowCluster", "route: unknown shadow cluster 'missing_shadow_cluster'",
       [](auto& rc, auto&, auto&) {
         rc.add_request_mirror_policies()->set_cluster("missing_shadow_cluster");
       }},
      {"MissingRouteShadowCluster", "route: unknown shadow cluster 'missing_route_shadow_cluster'",
       [](auto&, auto&, auto& r) {
         r.mutable_route()->add_request_mirror_policies()->set_cluster(
             "missing_route_shadow_cluster");
       }},
      {"OversizedDirectResponseBody", "response body size is 13 bytes; maximum is 4",
       [](auto& rc, auto&, auto& r) {
         rc.mutable_max_direct_response_body_size_bytes()->set_value(4);
         r.clear_route();
         r.mutable_direct_response()->set_status(200);
         r.mutable_direct_response()->mutable_body()->set_inline_string("too-long-body");
       }},
      {"InvalidVirtualHostHeader", ":-prefixed or host headers may not be modified",
       [](auto&, auto& vh, auto&) {
         addHeaderOption(vh.mutable_request_headers_to_add(), ":path", "/bad");
       }},
      {"UnknownVirtualHostPerFilterConfig",
       "Didn't find a registered implementation for 'unknown.non_optional.filter'",
       [](auto&, auto& vh, auto&) {
         (*vh.mutable_typed_per_filter_config())["unknown.non_optional.filter"] =
             makeTypeAny("google.protobuf.BoolValue");
       }},
      {"UnknownVettedVirtualHostPerFilterConfig",
       "Didn't find a registered implementation for 'unknown.non_optional.buffer_filter'",
       [](auto&, auto& vh, auto&) {
         (*vh.mutable_typed_per_filter_config())["unknown.non_optional.buffer_filter"] =
             makeBufferAny(1024);
       }},
      {"DuplicateWildcardDefaultDomain", "Only a single wildcard domain is permitted",
       addDupDomain("*", "*")},
      {"DuplicateExactDomain", "Only unique values for domains are permitted",
       addDupDomain("dup.com", "DUP.COM")},
      {"DuplicateSuffixWildcard", "Only unique values for domains are permitted",
       addDupDomain("*.dup.com", "*.DUP.COM")},
      {"DuplicatePrefixWildcard", "Only unique values for domains are permitted",
       addDupDomain("dup.*", "DUP.*")},
  };

  for (const auto& tc : cases) {
    SCOPED_TRACE(tc.name);
    RouteConfigProto rc = makeRouteConfig({makeBaseVhost("v1", "a.com", "valid_cluster")});
    auto* vh = rc.mutable_virtual_hosts(0);
    tc.mutate(rc, *vh, *vh->mutable_routes(0));
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

} // namespace
} // namespace Router
} // namespace Envoy
