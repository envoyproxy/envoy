// Per-request cost of the RBAC filter's decodeHeaders() for chains of header-only DENY policies.
//
// The policies are one that denies every method outside a fixed set and one that denies a path
// prefix, both compared without case. The cases are one and two such filters on a GET they allow,
// the same two filters with each policy also as shadow rules, a TRACE the first one denies, a route
// whose per-filter config overrides the rules stat prefix of the first filter, and a route whose
// per-filter config turns both filters off.
//
// The route comes from a real route table, so each per-route config lookup the filter makes walks
// the route, virtual host and route table maps by the filter's config name, as it does in the
// filter manager. The decoder callbacks answer the calls the filter makes on every request without
// gmock, whose dispatch would otherwise dominate those lookups. The denial still sends its local
// reply through the mock's encoder. Every request starts from empty dynamic metadata, as a new
// stream does.

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

constexpr absl::string_view MethodsFilterName = "envoy.filters.http.rbac.deny_unlisted_methods";
constexpr absl::string_view PathFilterName = "envoy.filters.http.rbac.deny_admin_path";

constexpr absl::string_view DenyUnlistedMethods = R"EOF(
rules:
  action: DENY
  policies:
    deny-unlisted-request-methods:
      permissions:
      - not_rule:
          or_rules:
            rules:
            - header: {name: ":method", string_match: {exact: get, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: put, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: patch, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: post, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: delete, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: options, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: head, ignore_case: true}}
            - header: {name: ":method", string_match: {exact: connect, ignore_case: true}}
      principals:
      - any: true
)EOF";

constexpr absl::string_view DenyPathPrefix = R"EOF(
rules:
  action: DENY
  policies:
    deny-admin-path:
      permissions:
      - header: {name: ":path", string_match: {prefix: /admin/, ignore_case: true}}
      principals:
      - any: true
)EOF";

enum class RouteShape {
  // The route, its virtual host and the route table carry no per-filter config.
  NoPerFilterConfig,
  // The route carries an RBACPerRoute for the first filter with its own rules stat prefix.
  RulesStatPrefixOverride,
  // The route carries an RBACPerRoute without `rbac` for both filters, which turns them off.
  RbacDisabled,
};

// The filter config for one policy. With `shadow`, the policy also runs as the shadow rules.
envoy::extensions::filters::http::rbac::v3::RBAC policy(absl::string_view yaml,
                                                        bool shadow = false) {
  envoy::extensions::filters::http::rbac::v3::RBAC config;
  TestUtility::loadFromYaml(std::string(yaml), config);
  if (shadow) {
    *config.mutable_shadow_rules() = config.rules();
  }
  return config;
}

envoy::config::route::v3::RouteConfiguration routeConfiguration(RouteShape shape) {
  envoy::config::route::v3::RouteConfiguration config;
  auto* vhost = config.add_virtual_hosts();
  vhost->set_name("bench");
  vhost->add_domains("*");
  auto* route = vhost->add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_direct_response()->set_status(200);

  auto& per_filter = *route->mutable_typed_per_filter_config();
  switch (shape) {
  case RouteShape::NoPerFilterConfig:
    break;
  case RouteShape::RulesStatPrefixOverride: {
    envoy::extensions::filters::http::rbac::v3::RBACPerRoute per_route;
    *per_route.mutable_rbac() = policy(DenyUnlistedMethods);
    per_route.mutable_rbac()->set_rules_stat_prefix("route_");
    std::ignore = per_filter[std::string(MethodsFilterName)].PackFrom(per_route);
    break;
  }
  case RouteShape::RbacDisabled:
    std::ignore = per_filter[std::string(MethodsFilterName)].PackFrom(
        envoy::extensions::filters::http::rbac::v3::RBACPerRoute());
    std::ignore = per_filter[std::string(PathFilterName)].PackFrom(
        envoy::extensions::filters::http::rbac::v3::RBACPerRoute());
    break;
  }
  return config;
}

Http::RequestHeaderMapPtr requestHeaders(absl::string_view method) {
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setMethod(method);
  headers->setPath("/v1/items/list?page_size=100");
  headers->setHost("bench.example.com");
  headers->setScheme("https");
  headers->setForwardedProto("https");
  headers->addCopy(Http::LowerCaseString("user-agent"), "bench-client/1.0");
  headers->addCopy(Http::LowerCaseString("accept"), "application/json");
  headers->addCopy(Http::LowerCaseString("x-request-id"), "9b2f6a50-4c1e-4d8e-9f3a-2b7c1d0e5f64");
  return headers;
}

// Answers connection(), streamInfo() and mostSpecificPerFilterConfig() directly. Every other call
// keeps the mock's default.
class BenchCallbacks : public Http::MockStreamDecoderFilterCallbacks {
public:
  BenchCallbacks(absl::string_view filter_name, const Router::Route& route,
                 const Network::Connection& connection, StreamInfo::StreamInfo& stream_info)
      : filter_name_(filter_name), route_(route), bench_connection_(connection),
        bench_stream_info_(stream_info) {}

  OptRef<const Network::Connection> connection() override { return bench_connection_; }
  StreamInfo::StreamInfo& streamInfo() override { return bench_stream_info_; }
  const Router::RouteSpecificFilterConfig* mostSpecificPerFilterConfig() const override {
    return route_.mostSpecificPerFilterConfig(filter_name_);
  }

private:
  const std::string filter_name_;
  const Router::Route& route_;
  const Network::Connection& bench_connection_;
  StreamInfo::StreamInfo& bench_stream_info_;
};

class Harness {
public:
  Harness(bool both_filters, RouteShape shape, bool shadow) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));

    auto route_config = Router::ConfigImpl::create(routeConfiguration(shape), factory_context_,
                                                   ProtobufMessage::getStrictValidationVisitor(),
                                                   factory_context_.initManager(), true);
    RELEASE_ASSERT(route_config.ok(), std::string(route_config.status().message()));
    route_config_ = std::move(*route_config);
    const auto lookup_headers = requestHeaders("GET");
    route_ = route_config_->route(*lookup_headers, stream_info_, 0).route;
    RELEASE_ASSERT(route_ != nullptr, "the route table matches no route");

    addFilter(MethodsFilterName, policy(DenyUnlistedMethods, shadow));
    if (both_filters) {
      addFilter(PathFilterName, policy(DenyPathPrefix, shadow));
    }
  }

  // The fixture's own share of every request, and the whole of what BM_Fixture measures.
  void resetStream() { stream_info_.dynamicMetadata() = envoy::config::core::v3::Metadata(); }

  // One request through the chain, which stops at the first filter that denies it.
  void oneRequest(Http::RequestHeaderMap& headers) {
    resetStream();
    for (size_t i = 0; i < configs_.size(); ++i) {
      RoleBasedAccessControlFilter filter(configs_[i]);
      filter.setDecoderFilterCallbacks(*callbacks_[i]);
      if (filter.decodeHeaders(headers, true) == Http::FilterHeadersStatus::StopIteration) {
        return;
      }
    }
  }

  uint64_t counter(const std::string& name) {
    Stats::CounterOptConstRef found = stats_store_.findCounterByString(name);
    RELEASE_ASSERT(found.has_value(), name);
    return found->value();
  }

  bool hasField(absl::string_view field) const {
    const auto& filter_metadata = stream_info_.dynamicMetadata().filter_metadata();
    const auto it = filter_metadata.find(DynamicMetadataKey);
    return it != filter_metadata.end() && it->second.fields().contains(field);
  }

  bool hasDynamicMetadata() const {
    return !stream_info_.dynamicMetadata().filter_metadata().empty();
  }

private:
  void addFilter(absl::string_view name,
                 const envoy::extensions::filters::http::rbac::v3::RBAC& config) {
    configs_.push_back(std::make_shared<RoleBasedAccessControlFilterConfig>(
        config, "bench.", *stats_store_.rootScope(), factory_context_,
        ProtobufMessage::getStrictValidationVisitor()));
    callbacks_.push_back(
        std::make_unique<NiceMock<BenchCallbacks>>(name, *route_, connection_, stream_info_));
  }

  Api::ApiPtr api_{Api::createApiForTest()};
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Stats::TestUtil::TestStore stats_store_;
  Event::TestRealTimeSystem time_system_;
  StreamInfo::StreamInfoImpl stream_info_{Http::Protocol::Http2, time_system_, nullptr,
                                          StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Network::MockConnection> connection_;
  std::shared_ptr<Router::ConfigImpl> route_config_;
  Router::RouteConstSharedPtr route_;
  std::vector<RoleBasedAccessControlFilterConfigSharedPtr> configs_;
  std::vector<std::unique_ptr<NiceMock<BenchCallbacks>>> callbacks_;
};

// The per-request work outside the filter, which resets the dynamic metadata.
void BM_Fixture(benchmark::State& state) {
  Harness harness(/*both_filters=*/true, RouteShape::NoPerFilterConfig, /*shadow=*/false);
  for (auto _ : state) { // NOLINT
    harness.resetStream();
  }
}
BENCHMARK(BM_Fixture);

// Runs one case and checks, before and after the timed loop, that every request took the path the
// case names, so a policy that stopped matching cannot pass for a faster filter.
void runCase(benchmark::State& state, bool both_filters, RouteShape shape, bool shadow,
             absl::string_view method, uint64_t allowed_per_request, uint64_t denied_per_request,
             absl::string_view expected_field) {
  Harness harness(both_filters, shape, shadow);
  auto headers = requestHeaders(method);

  harness.oneRequest(*headers);
  RELEASE_ASSERT(expected_field.empty() ? !harness.hasDynamicMetadata()
                                        : harness.hasField(expected_field),
                 "the case wrote unexpected dynamic metadata");

  uint64_t requests = 1;
  for (auto _ : state) { // NOLINT
    harness.oneRequest(*headers);
    ++requests;
  }

  RELEASE_ASSERT(harness.counter("bench.rbac.allowed") == requests * allowed_per_request,
                 "allowed count differs from the case");
  RELEASE_ASSERT(harness.counter("bench.rbac.denied") == requests * denied_per_request,
                 "denied count differs from the case");
  RELEASE_ASSERT(harness.counter("bench.rbac.shadow_allowed") ==
                     (shadow ? requests * allowed_per_request : 0),
                 "shadow allowed count differs from the case");
}

// Both filters allow a GET on a route with no per-filter config.
void BM_TwoFiltersAllowed(benchmark::State& state) {
  runCase(state, /*both_filters=*/true, RouteShape::NoPerFilterConfig, /*shadow=*/false, "GET",
          /*allowed_per_request=*/2, /*denied_per_request=*/0, "enforced_engine_result");
}
BENCHMARK(BM_TwoFiltersAllowed);

// Both filters also run their policy as shadow rules, and both engines of each allow a GET.
void BM_TwoFiltersShadowAndEnforcedAllowed(benchmark::State& state) {
  runCase(state, /*both_filters=*/true, RouteShape::NoPerFilterConfig, /*shadow=*/true, "GET",
          /*allowed_per_request=*/2, /*denied_per_request=*/0, "shadow_engine_result");
}
BENCHMARK(BM_TwoFiltersShadowAndEnforcedAllowed);

// The methods filter alone allows a GET on a route with no per-filter config.
void BM_OneFilterAllowed(benchmark::State& state) {
  runCase(state, /*both_filters=*/false, RouteShape::NoPerFilterConfig, /*shadow=*/false, "GET",
          /*allowed_per_request=*/1, /*denied_per_request=*/0, "enforced_engine_result");
}
BENCHMARK(BM_OneFilterAllowed);

// The methods filter denies a TRACE and sends a local reply, so the path filter never runs.
void BM_TwoFiltersDenied(benchmark::State& state) {
  runCase(state, /*both_filters=*/true, RouteShape::NoPerFilterConfig, /*shadow=*/false, "TRACE",
          /*allowed_per_request=*/0, /*denied_per_request=*/1, "enforced_effective_policy_id");
}
BENCHMARK(BM_TwoFiltersDenied);

// The route overrides the methods filter's rules stat prefix, which renames its fields.
void BM_TwoFiltersRulesStatPrefixOverride(benchmark::State& state) {
  runCase(state, /*both_filters=*/true, RouteShape::RulesStatPrefixOverride, /*shadow=*/false,
          "GET",
          /*allowed_per_request=*/2, /*denied_per_request=*/0, "route_enforced_engine_result");
}
BENCHMARK(BM_TwoFiltersRulesStatPrefixOverride);

// The route turns both filters off, so neither evaluates an engine or writes dynamic metadata.
void BM_TwoFiltersRbacDisabled(benchmark::State& state) {
  runCase(state, /*both_filters=*/true, RouteShape::RbacDisabled, /*shadow=*/false, "GET",
          /*allowed_per_request=*/0, /*denied_per_request=*/0, /*expected_field=*/"");
}
BENCHMARK(BM_TwoFiltersRbacDisabled);

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
