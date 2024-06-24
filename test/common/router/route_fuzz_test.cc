#include <functional>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/router/config_impl.h"

#include "test/common/router/route_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/instance.h"

namespace Envoy {
namespace Router {
namespace {

// Limit the size of the input.
static const size_t MaxInputSize = 64 * 1024;

bool isUnsupportedRouteConfig(const envoy::config::route::v3::Route& route) {
  if (route.has_filter_action() || route.has_non_forwarding_action()) {
    return true;
  }
  return false;
}

// Remove regex matching route configs.
envoy::config::route::v3::RouteConfiguration
cleanRouteConfig(envoy::config::route::v3::RouteConfiguration route_config) {
  envoy::config::route::v3::RouteConfiguration clean_config = route_config;
  auto virtual_hosts = clean_config.mutable_virtual_hosts();
  std::for_each(virtual_hosts->begin(), virtual_hosts->end(),
                [](envoy::config::route::v3::VirtualHost& virtual_host) {
                  auto routes = virtual_host.mutable_routes();
                  for (int i = 0; i < routes->size();) {
                    if (isUnsupportedRouteConfig(routes->Get(i))) {
                      routes->erase(routes->begin() + i);
                    } else {
                      ++i;
                    }
                  }
                });

  return clean_config;
}

bool validateMatcherConfig(const xds::type::matcher::v3::Matcher& matcher);

bool validateOnMatchConfig(const xds::type::matcher::v3::Matcher::OnMatch& on_match) {
  switch (on_match.on_match_case()) {
  case xds::type::matcher::v3::Matcher_OnMatch::kMatcher: {
    return validateMatcherConfig(on_match.matcher());
  }
  case xds::type::matcher::v3::Matcher_OnMatch::kAction: {
    // Only envoy...Route is allowed as typed_config here.
    if (on_match.action().typed_config().type_url().find("envoy.config.route.v3.Route") ==
        std::string::npos) {
      return false;
    }
    envoy::config::route::v3::Route on_match_route_action_config;
    THROW_IF_NOT_OK(
        MessageUtil::unpackTo(on_match.action().typed_config(), on_match_route_action_config));
    ENVOY_LOG_MISC(trace, "typed_config of on_match.action is: {}",
                   on_match_route_action_config.DebugString());
    return !isUnsupportedRouteConfig(on_match_route_action_config);
  }
  case xds::type::matcher::v3::Matcher_OnMatch::ON_MATCH_NOT_SET: {
    // This alternative should never occur. But of course it does. By returning false the
    // fuzzer will not follow this path any further.
    return false;
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool validateMatcherConfig(const xds::type::matcher::v3::Matcher& matcher) {
  if (matcher.has_matcher_list()) {
    if (std::any_of(
            std::begin(matcher.matcher_list().matchers()),
            std::end(matcher.matcher_list().matchers()),
            [](const xds::type::matcher::v3::Matcher::MatcherList::FieldMatcher& field_matcher) {
              return !validateOnMatchConfig(field_matcher.on_match());
            })) {
      ENVOY_LOG_MISC(debug, "matcher.matcher_list contains at least one entry that is invalid");
      return false;
    }
  } else if (matcher.has_matcher_tree()) {
    if (matcher.matcher_tree().has_exact_match_map()) {
      if (std::any_of(std::begin(matcher.matcher_tree().exact_match_map().map()),
                      std::end(matcher.matcher_tree().exact_match_map().map()),
                      [](const auto& matcher_entry) {
                        return !validateOnMatchConfig(matcher_entry.second);
                      })) {
        ENVOY_LOG_MISC(
            debug,
            "matcher.matcher_tree.exact_match_map contains at least one entry that is invalid");
        return false;
      }
    } else if (matcher.matcher_tree().has_prefix_match_map()) {
      if (std::any_of(std::begin(matcher.matcher_tree().prefix_match_map().map()),
                      std::end(matcher.matcher_tree().prefix_match_map().map()),
                      [](const auto& matcher_entry) {
                        return !validateOnMatchConfig(matcher_entry.second);
                      })) {
        ENVOY_LOG_MISC(debug, "matcher.matcher_tree.prefix_match_map contains at least one "
                              "entry that is invalid");
        return false;
      }
    }
  }
  if (!validateOnMatchConfig(matcher.on_no_match())) {
    ENVOY_LOG_MISC(debug, "matcher.on_no_match.action not sufficient for processing");
    return false;
  }
  return true;
}

// Check configuration for size and unimplemented/missing options.
bool validateConfig(const test::common::router::RouteTestCase& input) {
  const auto input_size = input.ByteSizeLong();
  if (input_size > MaxInputSize) {
    ENVOY_LOG_MISC(debug, "Input size {}kB exceeds {}kB ({}B > {}B).", input_size / 1024,
                   MaxInputSize / 1024, input_size, MaxInputSize);
    return false;
  }
  for (const auto& virtual_host : input.config().virtual_hosts()) {
    if (virtual_host.has_retry_policy_typed_config()) {
      ENVOY_LOG_MISC(debug, "retry_policy_typed_config: not implemented");
      return false;
    }
    if (virtual_host.has_matcher() && !validateMatcherConfig(virtual_host.matcher())) {
      return false;
    }
  }
  return true;
}

// TODO(htuch): figure out how to generate via a genrule from config_impl_test the full corpus.
DEFINE_PROTO_FUZZER(const test::common::router::RouteTestCase& input) {
  static NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  static NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  try {
    if (!validateConfig(input)) {
      return;
    }

    TestUtility::validate(input);
    const auto cleaned_route_config = cleanRouteConfig(input.config());
    ENVOY_LOG_MISC(debug, "cleaned route config: {}", cleaned_route_config.DebugString());
    std::shared_ptr<ConfigImpl> config =
        THROW_OR_RETURN_VALUE(ConfigImpl::create(cleaned_route_config, factory_context,
                                                 ProtobufMessage::getNullValidationVisitor(), true),
                              std::shared_ptr<ConfigImpl>);
    auto headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.headers());
    auto route = config->route(headers, stream_info, input.random_value());
    if (route != nullptr && route->routeEntry() != nullptr) {
      route->routeEntry()->finalizeRequestHeaders(headers, stream_info, true);
    }
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Router
} // namespace Envoy
