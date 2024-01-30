#include "test/tools/router_check/router.h"

#include <functional>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/macros.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/test_common/printers.h"

#include "absl/strings/str_format.h"

namespace {

const std::string toString(envoy::type::matcher::v3::StringMatcher::MatchPatternCase pattern) {
  switch (pattern) {
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kExact:
    return "exact";
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kPrefix:
    return "prefix";
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kSuffix:
    return "suffix";
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kSafeRegex:
    return "safe_regex";
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kContains:
    return "contains";
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::MATCH_PATTERN_NOT_SET:
    return "match_pattern_not_set";
  }
  PANIC("reached unexpected code");
}

const std::string toString(const envoy::config::route::v3::HeaderMatcher& header) {
  switch (header.header_match_specifier_case()) {
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch:
    return "exact_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch:
    return "safe_regex_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kRangeMatch:
    return "range_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPresentMatch:
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      HEADER_MATCH_SPECIFIER_NOT_SET:
    return "present_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch:
    return "prefix_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSuffixMatch:
    return "suffix_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kContainsMatch:
    return "contains_match";
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kStringMatch:
    return "string_match." + ::toString(header.string_match().match_pattern_case());
    break;
  }
  PANIC("reached unexpected code");
}

const std::string toString(const Envoy::Http::HeaderMap::GetResult& entry) {
  // TODO(mattklein123): Print multiple header values.
  return entry.empty() ? "NULL" : std::string(entry[0]->value().getStringView());
}

} // namespace

namespace Envoy {
// static
ToolConfig ToolConfig::create(const envoy::RouterCheckToolSchema::ValidationItem& check_config) {
  // Add header field values
  std::unique_ptr<Http::TestRequestHeaderMapImpl> request_headers(
      new Http::TestRequestHeaderMapImpl());
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers(
      new Http::TestResponseHeaderMapImpl());
  request_headers->addCopy(":authority", check_config.input().authority());
  request_headers->addCopy(":path", check_config.input().path());
  request_headers->addCopy(":method", check_config.input().method());
  request_headers->addCopy("x-forwarded-proto", check_config.input().ssl() ? "https" : "http");
  request_headers->addCopy(":scheme", check_config.input().ssl() ? "https" : "http");

  if (check_config.input().internal()) {
    request_headers->addCopy("x-envoy-internal", "true");
  }

  if (check_config.input().additional_request_headers().data()) {
    for (const envoy::config::core::v3::HeaderValue& header_config :
         check_config.input().additional_request_headers()) {
      request_headers->addCopy(header_config.key(), header_config.value());
    }
  }

  if (check_config.input().additional_response_headers().data()) {
    for (const envoy::config::core::v3::HeaderValue& header_config :
         check_config.input().additional_response_headers()) {
      response_headers->addCopy(header_config.key(), header_config.value());
    }
  }

  return {std::move(request_headers), std::move(response_headers),
          static_cast<int>(check_config.input().random_value())};
}

ToolConfig::ToolConfig(std::unique_ptr<Http::TestRequestHeaderMapImpl> request_headers,
                       std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers,
                       int random_value)
    : request_headers_(std::move(request_headers)), response_headers_(std::move(response_headers)),
      random_value_(random_value) {}

// static
RouterCheckTool RouterCheckTool::create(const std::string& router_config_file,
                                        const bool disable_deprecation_check) {
  // TODO(hennna): Allow users to load a full config and extract the route configuration from it.
  envoy::config::route::v3::RouteConfiguration route_config;
  auto stats = std::make_unique<Stats::IsolatedStoreImpl>();
  auto api = Api::createApiForTest(*stats);
  TestUtility::loadFromFile(router_config_file, route_config, *api);
  assignUniqueRouteNames(route_config);
  assignRuntimeFraction(route_config);
  auto factory_context =
      std::make_unique<NiceMock<Server::Configuration::MockServerFactoryContext>>();
  auto config = std::make_unique<Router::ConfigImpl>(
      route_config, *factory_context, ProtobufMessage::getNullValidationVisitor(), false);
  if (!disable_deprecation_check) {
    ProtobufMessage::StrictValidationVisitorImpl visitor;
    visitor.setRuntime(factory_context->runtime_loader_);
    MessageUtil::checkForUnexpectedFields(route_config, visitor);
  }

  return {std::move(factory_context), std::move(config), std::move(stats), std::move(api),
          Coverage(route_config)};
}

void RouterCheckTool::assignUniqueRouteNames(
    envoy::config::route::v3::RouteConfiguration& route_config) {
  Random::RandomGeneratorImpl random;
  for (auto& host : *route_config.mutable_virtual_hosts()) {
    for (auto& route : *host.mutable_routes()) {
      std::string name = absl::StrFormat("%s-%s", route.name(), random.uuid());
      route.set_name(name);
    }
  }
}

void RouterCheckTool::assignRuntimeFraction(
    envoy::config::route::v3::RouteConfiguration& route_config) {
  for (auto& host : *route_config.mutable_virtual_hosts()) {
    for (auto& route : *host.mutable_routes()) {
      if (route.match().has_runtime_fraction() &&
          route.match().runtime_fraction().default_value().numerator() == 0) {
        route.mutable_match()->mutable_runtime_fraction()->mutable_default_value()->set_numerator(
            1);
      }
    }
  }
}

void RouterCheckTool::finalizeHeaders(ToolConfig& tool_config,
                                      Envoy::StreamInfo::StreamInfoImpl stream_info) {
  if (!headers_finalized_ && tool_config.route_ != nullptr) {
    if (tool_config.route_->directResponseEntry() != nullptr) {
      tool_config.route_->directResponseEntry()->rewritePathHeader(*tool_config.request_headers_,
                                                                   true);
      sendLocalReply(tool_config, *tool_config.route_->directResponseEntry());
      tool_config.route_->directResponseEntry()->finalizeResponseHeaders(
          *tool_config.response_headers_, stream_info);
    } else if (tool_config.route_->routeEntry() != nullptr) {
      tool_config.route_->routeEntry()->finalizeRequestHeaders(*tool_config.request_headers_,
                                                               stream_info, true);
      tool_config.route_->routeEntry()->finalizeResponseHeaders(*tool_config.response_headers_,
                                                                stream_info);
    }
  }

  headers_finalized_ = true;
}

void RouterCheckTool::sendLocalReply(ToolConfig& tool_config,
                                     const Router::DirectResponseEntry& entry) {
  auto encode_functions = Envoy::Http::Utility::EncodeFunctions{
      nullptr, nullptr,
      [&](Envoy::Http::ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
        UNREFERENCED_PARAMETER(end_stream);
        Http::HeaderMapImpl::copyFrom(*tool_config.response_headers_->header_map_, *headers);
      },
      [&](Envoy::Buffer::Instance& data, bool end_stream) -> void {
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(end_stream);
      }};

  bool is_grpc = false;
  bool is_head_request = false;
  Envoy::Http::Utility::LocalReplyData local_reply_data{
      is_grpc, entry.responseCode(), entry.responseBody(), absl::nullopt, is_head_request};

  Envoy::Http::Utility::sendLocalReply(false, encode_functions, local_reply_data);
}

RouterCheckTool::RouterCheckTool(
    std::unique_ptr<NiceMock<Server::Configuration::MockServerFactoryContext>> factory_context,
    std::unique_ptr<Router::ConfigImpl> config, std::unique_ptr<Stats::IsolatedStoreImpl> stats,
    Api::ApiPtr api, Coverage coverage)
    : factory_context_(std::move(factory_context)), config_(std::move(config)),
      stats_(std::move(stats)), api_(std::move(api)), coverage_(std::move(coverage)) {
  ON_CALL(factory_context_->runtime_loader_.snapshot_,
          featureEnabled(_, testing::An<const envoy::type::v3::FractionalPercent&>(),
                         testing::An<uint64_t>()))
      .WillByDefault(testing::Invoke(this, &RouterCheckTool::runtimeMock));
}

Json::ObjectSharedPtr loadFromFile(const std::string& file_path, Api::Api& api) {
  std::string contents = api.fileSystem().fileReadToEnd(file_path).value();
  if (absl::EndsWith(file_path, ".yaml")) {
    contents = MessageUtil::getJsonStringFromMessageOrError(ValueUtil::loadFromYaml(contents));
  }
  return Json::Factory::loadFromString(contents);
}

std::vector<envoy::RouterCheckToolSchema::ValidationItemResult>
RouterCheckTool::compareEntries(const std::string& expected_routes) {
  envoy::RouterCheckToolSchema::Validation validation_config;
  auto stats = std::make_unique<Stats::IsolatedStoreImpl>();
  auto api = Api::createApiForTest(*stats);
  const std::string contents = api->fileSystem().fileReadToEnd(expected_routes).value();
  TestUtility::loadFromFile(expected_routes, validation_config, *api);
  TestUtility::validate(validation_config);

  std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results;
  for (const envoy::RouterCheckToolSchema::ValidationItem& check_config :
       validation_config.tests()) {
    active_runtime_ = check_config.input().runtime();
    headers_finalized_ = false;
    auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
        nullptr, Network::Utility::getCanonicalIpv4LoopbackAddress());
    Envoy::StreamInfo::StreamInfoImpl stream_info(
        Envoy::Http::Protocol::Http11, factory_context_->mainThreadDispatcher().timeSource(),
        connection_info_provider);
    ToolConfig tool_config = ToolConfig::create(check_config);
    tool_config.route_ =
        config_->route(*tool_config.request_headers_, stream_info, tool_config.random_value_);

    const std::string& test_name = check_config.test_name();
    tests_.emplace_back(test_name, std::vector<std::string>{});
    const envoy::RouterCheckToolSchema::ValidationAssert& validate = check_config.validate();

    using CheckerFunc =
        std::function<bool(ToolConfig&, const envoy::RouterCheckToolSchema::ValidationAssert&,
                           envoy::RouterCheckToolSchema::ValidationFailure&)>;
    CheckerFunc checkers[] = {
        [this](auto&... params) -> bool { return this->compareCluster(params...); },
        [this](auto&... params) -> bool { return this->compareVirtualCluster(params...); },
        [this](auto&... params) -> bool { return this->compareVirtualHost(params...); },
        [this](auto&... params) -> bool { return this->compareRewritePath(params...); },
        [this](auto&... params) -> bool { return this->compareRewriteHost(params...); },
        [this](auto&... params) -> bool { return this->compareRedirectPath(params...); },
        [this](auto&... params) -> bool { return this->compareRedirectCode(params...); },
        [this](auto&... params) -> bool { return this->compareRequestHeaderFields(params...); },
        [this](auto&... params) -> bool { return this->compareResponseHeaderFields(params...); },
    };
    finalizeHeaders(tool_config, stream_info);

    // Call appropriate function for each match case.
    bool test_failed = false;
    envoy::RouterCheckToolSchema::ValidationItemResult test_result;
    test_result.set_test_name(test_name);
    envoy::RouterCheckToolSchema::ValidationFailure validation_failure;
    for (const auto& test : checkers) {
      if (!test(tool_config, validate, validation_failure)) {
        test_failed = true;
      }
    }
    test_result.set_test_passed(!test_failed);
    if (test_failed) {
      *test_result.mutable_failure() = validation_failure;
    }
    if (test_failed || !only_show_failures_) {
      test_results.push_back(test_result);
    }
  }
  printResults();
  return test_results;
}

bool RouterCheckTool::compareCluster(ToolConfig& tool_config,
                                     const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                                     envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_cluster_name()) {
    return true;
  }
  const bool has_route_entry =
      tool_config.route_ != nullptr && tool_config.route_->routeEntry() != nullptr;
  std::string actual = "";
  if (has_route_entry) {
    actual = tool_config.route_->routeEntry()->clusterName();
  }
  const bool matches = compareResults(actual, expected.cluster_name().value(), "cluster_name");
  if (!matches) {
    failure.mutable_expected_cluster_name()->set_value(expected.cluster_name().value());
    failure.mutable_actual_cluster_name()->set_value(actual);
  }
  if (matches && has_route_entry) {
    coverage_.markClusterCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareVirtualCluster(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_virtual_cluster_name()) {
    return true;
  }
  const bool has_route_entry =
      tool_config.route_ != nullptr && tool_config.route_->routeEntry() != nullptr;
  const bool has_virtual_cluster =
      has_route_entry &&
      tool_config.route_->routeEntry()->virtualCluster(*tool_config.request_headers_) != nullptr;
  std::string actual = "";
  if (has_virtual_cluster) {
    Stats::StatName stat_name =
        tool_config.route_->routeEntry()->virtualCluster(*tool_config.request_headers_)->statName();
    actual = tool_config.symbolTable().toString(stat_name);
  }
  const bool matches =
      compareResults(actual, expected.virtual_cluster_name().value(), "virtual_cluster_name");
  if (!matches) {
    failure.mutable_expected_virtual_cluster_name()->set_value(
        expected.virtual_cluster_name().value());
    failure.mutable_actual_virtual_cluster_name()->set_value(actual);
  }
  if (matches && has_route_entry) {
    coverage_.markVirtualClusterCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareVirtualHost(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_virtual_host_name()) {
    return true;
  }
  const bool has_route_entry =
      tool_config.route_ != nullptr && tool_config.route_->routeEntry() != nullptr;
  std::string actual = "";
  if (has_route_entry) {
    Stats::StatName stat_name = tool_config.route_->routeEntry()->virtualHost().statName();
    actual = tool_config.symbolTable().toString(stat_name);
  }
  const bool matches =
      compareResults(actual, expected.virtual_host_name().value(), "virtual_host_name");
  if (!matches) {
    failure.mutable_expected_virtual_host_name()->set_value(expected.virtual_host_name().value());
    failure.mutable_actual_virtual_host_name()->set_value(actual);
  }
  if (matches && has_route_entry) {
    coverage_.markVirtualHostCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareRewritePath(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_path_rewrite()) {
    return true;
  }
  const bool has_route_entry =
      tool_config.route_ != nullptr && tool_config.route_->routeEntry() != nullptr;
  std::string actual = "";
  if (has_route_entry) {
    actual = tool_config.request_headers_->get_(Http::Headers::get().Path);
  }
  const bool matches = compareResults(actual, expected.path_rewrite().value(), "path_rewrite");
  if (!matches) {
    failure.mutable_expected_path_rewrite()->set_value(expected.path_rewrite().value());
    failure.mutable_actual_path_rewrite()->set_value(actual);
  }
  if (matches && has_route_entry) {
    coverage_.markPathRewriteCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareRewriteHost(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_host_rewrite()) {
    return true;
  }
  const bool has_route_entry =
      tool_config.route_ != nullptr && tool_config.route_->routeEntry() != nullptr;
  std::string actual = "";
  if (has_route_entry) {
    actual = tool_config.request_headers_->get_(Http::Headers::get().Host);
  }
  const bool matches = compareResults(actual, expected.host_rewrite().value(), "host_rewrite");
  if (!matches) {
    failure.mutable_expected_host_rewrite()->set_value(expected.host_rewrite().value());
    failure.mutable_actual_host_rewrite()->set_value(actual);
  }
  if (matches && has_route_entry) {
    coverage_.markHostRewriteCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareRedirectPath(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_path_redirect()) {
    return true;
  }
  const bool has_direct_response_entry =
      tool_config.route_ != nullptr && tool_config.route_->directResponseEntry() != nullptr;
  std::string actual = "";
  if (has_direct_response_entry) {
    actual = tool_config.route_->directResponseEntry()->newUri(*tool_config.request_headers_);
  }
  const bool matches = compareResults(actual, expected.path_redirect().value(), "path_redirect");
  if (!matches) {
    failure.mutable_expected_path_redirect()->set_value(expected.path_redirect().value());
    failure.mutable_actual_path_redirect()->set_value(actual);
  }
  if (matches && has_direct_response_entry) {
    coverage_.markRedirectPathCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareRedirectCode(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  if (!expected.has_code_redirect()) {
    return true;
  }
  const bool has_direct_response_entry =
      tool_config.route_ != nullptr && tool_config.route_->directResponseEntry() != nullptr;
  uint32_t actual = 0;
  if (has_direct_response_entry) {
    actual = Envoy::enumToInt(tool_config.route_->directResponseEntry()->responseCode());
  }
  const bool matches = compareResults(actual, expected.code_redirect().value(), "code_redirect");
  if (!matches) {
    failure.mutable_expected_code_redirect()->set_value(expected.code_redirect().value());
    failure.mutable_actual_code_redirect()->set_value(actual);
  }
  if (matches && has_direct_response_entry) {
    coverage_.markRedirectCodeCovered(tool_config.route_);
  }
  return matches;
}

bool RouterCheckTool::compareRequestHeaderFields(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  bool no_failures = true;
  if (expected.request_header_matches().data()) {
    for (const envoy::config::route::v3::HeaderMatcher& header :
         expected.request_header_matches()) {
      envoy::RouterCheckToolSchema::HeaderMatchFailure header_match_failure;
      if (!matchHeaderField(*tool_config.request_headers_, header, "request_header_matches",
                            header_match_failure)) {
        *failure.add_request_header_match_failures() = header_match_failure;
        no_failures = false;
      }
    }
  }
  // TODO(kb000) : Remove deprecated request_header_fields.
  if (expected.request_header_fields().data()) {
    for (const envoy::config::core::v3::HeaderValue& header : expected.request_header_fields()) {
      auto actual = tool_config.request_headers_->get_(header.key());
      auto const& expected = header.value();
      if (!compareResults(actual, expected, "request_header_fields")) {
        no_failures = false;
      }
    }
  }
  return no_failures;
}

bool RouterCheckTool::compareResponseHeaderFields(
    ToolConfig& tool_config, const envoy::RouterCheckToolSchema::ValidationAssert& expected,
    envoy::RouterCheckToolSchema::ValidationFailure& failure) {
  bool no_failures = true;
  if (expected.response_header_matches().data()) {
    for (const envoy::config::route::v3::HeaderMatcher& header :
         expected.response_header_matches()) {
      envoy::RouterCheckToolSchema::HeaderMatchFailure header_match_failure;
      if (!matchHeaderField(*tool_config.response_headers_, header, "response_header_matches",
                            header_match_failure)) {
        *failure.add_response_header_match_failures() = header_match_failure;
        no_failures = false;
      }
    }
  }
  // TODO(kb000) : Remove deprecated response_header_fields.
  if (expected.response_header_fields().data()) {
    for (const envoy::config::core::v3::HeaderValue& header : expected.response_header_fields()) {
      auto actual = tool_config.response_headers_->get_(header.key());
      auto const& expected = header.value();
      if (!compareResults(actual, expected, "response_header_fields")) {
        no_failures = false;
      }
    }
  }
  return no_failures;
}

template <typename HeaderMap>
bool RouterCheckTool::matchHeaderField(
    const HeaderMap& header_map, const envoy::config::route::v3::HeaderMatcher& header,
    const std::string test_type,
    envoy::RouterCheckToolSchema::HeaderMatchFailure& header_match_failure) {
  Envoy::Http::HeaderUtility::HeaderData expected_header_data{header};
  if (Envoy::Http::HeaderUtility::matchHeaders(header_map, expected_header_data)) {
    return true;
  }

  header_match_failure.mutable_header_matcher()->CopyFrom(header);
  const Http::LowerCaseString header_key(header.name());
  const auto header_value = header_map.get(header_key);
  header_match_failure.mutable_actual_header_value()->set_value(
      header_value.empty() ? "" : ::toString(header_value));

  // Test failed. Decide on what to log.
  std::string actual, expected;
  std::string match_test_type{test_type + "." + ::toString(header)};
  switch (header.header_match_specifier_case()) {
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPresentMatch:
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      HEADER_MATCH_SPECIFIER_NOT_SET:
    actual = "has(" + header.name() + "):" + (header.invert_match() ? "true" : "false");
    expected = "has(" + header.name() + "):" + (header.invert_match() ? "false" : "true");
    reportFailure(actual, expected, match_test_type);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kStringMatch:
    if (header.string_match().match_pattern_case() ==
        envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kExact) {
      actual = header.name() + ": " + ::toString(header_value);
      expected = header.name() + ": " + header.string_match().exact();
      reportFailure(actual, expected, match_test_type, !header.invert_match());
      break;
    }
    FALLTHRU;
  default:
    actual = header.name() + ": " + ::toString(header_value);
    tests_.back().second.emplace_back("actual: [" + actual + "], test type: " + match_test_type);
    break;
  }

  return false;
}

template <typename U, typename V>
bool RouterCheckTool::compareResults(const U& actual, const V& expected,
                                     const std::string& test_type, const bool expect_match) {
  if ((expected == actual) != expect_match) {
    reportFailure(actual, expected, test_type, expect_match);
    return false;
  }
  return true;
}

template <typename U, typename V>
void RouterCheckTool::reportFailure(const U& actual, const V& expected,
                                    const std::string& test_type, const bool expect_match) {
  std::stringstream ss;
  ss << "expected: [" << expected << "], actual: " << (expect_match ? "" : "NOT ") << "[" << actual
     << "],"
     << " test type: " << test_type;
  tests_.back().second.emplace_back(ss.str());
}

void RouterCheckTool::printResults() {
  // Output failure details to stdout if details_ flag is set to true
  for (const auto& test_result : tests_) {
    // All test names are printed if the details_ flag is true unless only_show_failures_ is
    // also true.
    if ((details_ && !only_show_failures_) ||
        (only_show_failures_ && !test_result.second.empty())) {
      if (test_result.second.empty()) {
        std::cout << test_result.first << std::endl;
      } else {
        std::cerr << test_result.first << std::endl;
        for (const auto& failure : test_result.second) {
          std::cerr << failure << std::endl;
        }
      }
    }
  }
}

// The Mock for runtime value checks.
// This is a simple implementation to mimic the actual runtime checks in Snapshot.featureEnabled
bool RouterCheckTool::runtimeMock(absl::string_view key,
                                  const envoy::type::v3::FractionalPercent& default_value,
                                  uint64_t random_value) {
  return !active_runtime_.empty() && key.compare(active_runtime_) == 0 &&
         ProtobufPercentHelper::evaluateFractionalPercent(default_value, random_value);
}

Options::Options(int argc, char** argv) {
  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  TCLAP::CmdLine cmd("router_check_tool", ' ', "none", true);
  TCLAP::SwitchArg is_detailed("d", "details", "Show detailed test execution results", cmd, false);
  TCLAP::SwitchArg only_show_failures("", "only-show-failures", "Only display failing tests", cmd,
                                      false);
  TCLAP::SwitchArg disable_deprecation_check("", "disable-deprecation-check",
                                             "Disable deprecated fields check", cmd, false);
  TCLAP::ValueArg<double> fail_under("f", "fail-under",
                                     "Fail if test coverage is under a specified amount", false,
                                     0.0, "float", cmd);
  TCLAP::SwitchArg comprehensive_coverage(
      "", "covall", "Measure coverage by checking all route fields", cmd, false);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file.", false,
                                           "", "string", cmd);
  TCLAP::ValueArg<std::string> test_path("t", "test-path", "Path to test file.", false, "",
                                         "string", cmd);
  TCLAP::ValueArg<std::string> output_path(
      "o", "output-path", "Path to output file to write test results", false, "", "string", cmd);
  TCLAP::UnlabeledMultiArg<std::string> unlabelled_configs(
      "unlabelled-configs", "unlabelled configs", false, "unlabelledConfigStrings", cmd);
  TCLAP::SwitchArg detailed_coverage(
      "", "detailed-coverage", "Show detailed coverage with routes without tests", cmd, false);
  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    std::cerr << "error: " << e.error() << std::endl;
    exit(EXIT_FAILURE);
  }

  is_detailed_ = is_detailed.getValue();
  only_show_failures_ = only_show_failures.getValue();
  fail_under_ = fail_under.getValue();
  comprehensive_coverage_ = comprehensive_coverage.getValue();
  disable_deprecation_check_ = disable_deprecation_check.getValue();
  detailed_coverage_report_ = detailed_coverage.getValue();

  config_path_ = config_path.getValue();
  test_path_ = test_path.getValue();
  output_path_ = output_path.getValue();
  if (config_path_.empty() || test_path_.empty()) {
    std::cerr << "error: "
              << "Both --config-path/c and --test-path/t are mandatory" << std::endl;
    exit(EXIT_FAILURE);
  }
}
} // namespace Envoy
