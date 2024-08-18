#pragma once

#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
#include "source/common/router/config_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"
#include "test/tools/router_check/coverage.h"
#include "test/tools/router_check/validation.pb.h"
#include "test/tools/router_check/validation.pb.validate.h"

#include "tclap/CmdLine.h"

namespace Envoy {
/**
 * Struct that stores the configuration parameters of the router check tool extracted from a json
 * input file.
 */
struct ToolConfig {
  ToolConfig() = default;

  /**
   * @param check_config tool config proto object.
   * @return ToolConfig a ToolConfig instance with member variables set by the tool config json
   * file.
   */
  static ToolConfig create(const envoy::RouterCheckToolSchema::ValidationItem& check_config);

  Stats::SymbolTable& symbolTable() { return *symbol_table_; }

  std::unique_ptr<Http::TestRequestHeaderMapImpl> request_headers_;
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers_;
  Router::RouteConstSharedPtr route_;
  int random_value_{0};

private:
  ToolConfig(std::unique_ptr<Http::TestRequestHeaderMapImpl> request_headers,
             std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers, int random_value);
  Stats::TestUtil::TestSymbolTable symbol_table_;
};

/**
 * Options for Router Check Tool.
 */
struct Options {
  Options();
  Options(int argc, char** argv);

  Options& setConfigPath(std::string path) {
    config_path = path;
    return *this;
  }
  Options& setBootstrapConfig(std::string path) {
    bootstrap_config = path;
    return *this;
  }
  Options& setTestPath(std::string path) {
    test_path = path;
    return *this;
  }
  Options& setFailUnder(double value) {
    fail_under = value;
    return *this;
  }
  Options& setComprehensiveCoverage(bool value) {
    comprehensive_coverage = value;
    return *this;
  }
  Options& setDetailed(bool value) {
    is_detailed = value;
    return *this;
  }
  Options& setOnlyShowFailures(bool value) {
    only_show_failures = value;
    return *this;
  }
  Options& setDisableDeprecationCheck(bool value) {
    disable_deprecation_check = value;
    return *this;
  }
  Options& setDetailedCoverageReport(bool value) {
    detailed_coverage_report = value;
    return *this;
  }
  Options& setOutputPath(std::string path) {
    output_path = path;
    return *this;
  }
  Options& setListenerName(std::string listener) {
    listener_name = listener;
    return *this;
  }

  absl::Status validate() const {
    if (test_path.empty()) {
      return absl::InvalidArgumentError("test path is required");
    }
    if (config_path.empty() && bootstrap_config.empty()) {
      return absl::InvalidArgumentError("route or bootstrap config are required");
    }
    if (!config_path.empty() && !bootstrap_config.empty()) {
      return absl::InvalidArgumentError("only one of route or bootstrap config is allowed");
    }
    if (!bootstrap_config.empty() && listener_name.empty()) {
      return absl::InvalidArgumentError("listener name is required when using bootstrap config");
    }
    return absl::OkStatus();
  }

  std::string test_path;
  std::string config_path;
  std::string bootstrap_config;
  float fail_under;
  bool comprehensive_coverage;
  bool is_detailed;
  bool only_show_failures;
  bool disable_deprecation_check;
  bool detailed_coverage_report;
  std::string output_path;
  std::string listener_name;
};

/**
 * A route table check tool that check whether route parameters returned by a router match
 * what is expected.
 */
class RouterCheckTool : Logger::Loggable<Logger::Id::testing> {
public:
  /**
   * @param options RouterCheckTool options.
   * @return RouterCheckTool a RouterCheckTool instance
   */
  static RouterCheckTool create(const Options& options);
  /**
   * @param expected_route_json tool config json file.
   * @return bool if all routes match what is expected.
   */
  std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> compareEntries();

  float coverage() {
    return options_.detailed_coverage_report ? coverage_.comprehensiveReport()
                                             : coverage_.report(options_.detailed_coverage_report);
  }

private:
  RouterCheckTool(
      std::unique_ptr<NiceMock<Server::Configuration::MockServerFactoryContext>> factory_context,
      std::shared_ptr<Router::ConfigImpl> config, std::unique_ptr<Stats::IsolatedStoreImpl> stats,
      Api::ApiPtr api, Coverage coverage, Options options);

  /**
   * Create route check tool from a route typed configuration.
   */
  static RouterCheckTool createFromRoute(const Options& options);

  /**
   * Create route check tool from a bootstrap typed configuration.
   */
  static RouterCheckTool createFromBootstrap(const Options& options);

  /**
   * Extracts the route configuration from the listener with the given name.
   */
  static absl::optional<envoy::config::route::v3::RouteConfiguration>
  extractRouteConfigFromListener(envoy::config::bootstrap::v3::Bootstrap bootstrap_config,
                                 const std::string& listener_name);
  /**
   * Set UUID as the name for each route for detecting missing tests during the coverage check.
   */
  static void assignUniqueRouteNames(envoy::config::route::v3::RouteConfiguration& route_config);

  /**
   * For each route with runtime fraction 0%, set the numerator to a nonzero value so the
   * route can be tested as enabled or disabled.
   */
  static void assignRuntimeFraction(envoy::config::route::v3::RouteConfiguration& route_config);

  /**
   * Perform header transforms for any request/response headers for the route matched.
   * Can be called at most once for each test route.
   */
  void finalizeHeaders(ToolConfig& tool_config, Envoy::StreamInfo::StreamInfoImpl stream_info);

  /*
   * Performs direct-response reply actions for a response entry.
   */
  void sendLocalReply(ToolConfig& tool_config, const Router::DirectResponseEntry& entry);

  bool compareCluster(ToolConfig& tool_config,
                      const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                      envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareVirtualCluster(ToolConfig& tool_config,
                             const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                             envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareVirtualHost(ToolConfig& tool_config,
                          const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                          envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareRewriteHost(ToolConfig& tool_config,
                          const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                          envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareRewritePath(ToolConfig& tool_config,
                          const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                          envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareRedirectPath(ToolConfig& tool_config,
                           const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                           envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareRedirectCode(ToolConfig& tool_config,
                           const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                           envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareRequestHeaderFields(ToolConfig& tool_config,
                                  const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                                  envoy::RouterCheckToolSchema::ValidationFailure& failure);
  bool compareResponseHeaderFields(ToolConfig& tool_config,
                                   const envoy::RouterCheckToolSchema::ValidationAssert& expected,
                                   envoy::RouterCheckToolSchema::ValidationFailure& failure);
  template <typename HeaderMap>
  bool matchHeaderField(const HeaderMap& header_map,
                        const envoy::config::route::v3::HeaderMatcher& header,
                        const std::string test_type,
                        envoy::RouterCheckToolSchema::HeaderMatchFailure& header_match_failure);

  /**
   * Compare the expected and actual route parameter values. Print out match details if details_
   * flag is set.
   * @param actual holds the actual route returned by the router.
   * @param expected holds the expected parameter value of the route.
   * @param expect_match negates the expectation if false.
   * @return bool if actual and expected match.
   */
  template <typename U, typename V>
  bool compareResults(const U& actual, const V& expected, const std::string& test_type,
                      const bool expect_match = true);

  template <typename U, typename V>
  void reportFailure(const U& actual, const V& expected, const std::string& test_type,
                     const bool expect_match = true);

  void printResults();

  bool runtimeMock(absl::string_view key, const envoy::type::v3::FractionalPercent& default_value,
                   uint64_t random_value);

  bool headers_finalized_{false};

  // The first member of each pair is the name of the test.
  // The second member is a list of any failing results for that test as strings.
  std::vector<std::pair<std::string, std::vector<std::string>>> tests_;

  // TODO(hennna): Switch away from mocks following work done by @rlazarus in github issue #499.
  std::unique_ptr<NiceMock<Server::Configuration::MockServerFactoryContext>> factory_context_;
  std::shared_ptr<Router::ConfigImpl> config_;
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_;
  Api::ApiPtr api_;
  std::string active_runtime_;
  Coverage coverage_;
  Options options_;
};
} // namespace Envoy
