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
 * A route table check tool that check whether route parameters returned by a router match
 * what is expected.
 */
class RouterCheckTool : Logger::Loggable<Logger::Id::testing> {
public:
  /**
   * @param router_config_file v2 router config file.
   * @param disable_deprecation_check flag to disable the RouteConfig deprecated field check
   * @return RouterCheckTool a RouterCheckTool instance with member variables set by the router
   * config file.
   * */
  static RouterCheckTool create(const std::string& router_config_file,
                                const bool disable_deprecation_check);

  /**
   * @param expected_route_json tool config json file.
   * @return bool if all routes match what is expected.
   */
  std::vector<envoy::RouterCheckToolSchema::ValidationItemResult>
  compareEntries(const std::string& expected_routes);

  /**
   * Set whether to print out match case details.
   */
  void setShowDetails() { details_ = true; }

  /**
   * Set whether to only print failing match cases.
   */
  void setOnlyShowFailures() { only_show_failures_ = true; }

  /**
   * Set whether to print out detailed coverage report.
   */
  void setDetailedCoverageReport() { detailed_coverage_report_ = true; }

  float coverage(bool comprehensive_coverage) {
    return comprehensive_coverage ? coverage_.comprehensiveReport()
                                  : coverage_.report(detailed_coverage_report_);
  }

private:
  RouterCheckTool(
      std::unique_ptr<NiceMock<Server::Configuration::MockServerFactoryContext>> factory_context,
      std::unique_ptr<Router::ConfigImpl> config, std::unique_ptr<Stats::IsolatedStoreImpl> stats,
      Api::ApiPtr api, Coverage coverage);

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

  bool details_{false};

  bool only_show_failures_{false};

  bool detailed_coverage_report_{false};

  // The first member of each pair is the name of the test.
  // The second member is a list of any failing results for that test as strings.
  std::vector<std::pair<std::string, std::vector<std::string>>> tests_;

  // TODO(hennna): Switch away from mocks following work done by @rlazarus in github issue #499.
  std::unique_ptr<NiceMock<Server::Configuration::MockServerFactoryContext>> factory_context_;
  std::unique_ptr<Router::ConfigImpl> config_;
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_;
  Api::ApiPtr api_;
  std::string active_runtime_;
  Coverage coverage_;
};

/**
 * Parses command line arguments for Router Check Tool.
 */
class Options {
public:
  Options(int argc, char** argv);

  /**
   * @return the path to configuration file.
   */
  const std::string& configPath() const { return config_path_; }

  /**
   * @return the path to test file.
   */
  const std::string& testPath() const { return test_path_; }

  /**
   * @return the minimum required percentage of routes coverage.
   */
  double failUnder() const { return fail_under_; }

  /**
   * @return true if test coverage should be comprehensive.
   */
  bool comprehensiveCoverage() const { return comprehensive_coverage_; }

  /**
   * @return true if detailed test execution results are displayed.
   */
  bool isDetailed() const { return is_detailed_; }

  /**
   * @return true if only test failures are displayed.
   */
  bool onlyShowFailures() const { return only_show_failures_; }

  /**
   * @return true if the deprecated field check for RouteConfiguration is disabled.
   */
  bool disableDeprecationCheck() const { return disable_deprecation_check_; }

  /**
   * @return true if detailed coverage report is displayed.
   */
  bool detailedCoverageReport() const { return detailed_coverage_report_; }

  /**
   * @return the path to save tests results.
   */
  const std::string& outputPath() const { return output_path_; }

private:
  std::string test_path_;
  std::string config_path_;
  float fail_under_;
  bool comprehensive_coverage_;
  bool is_detailed_;
  bool only_show_failures_;
  bool disable_deprecation_check_;
  bool detailed_coverage_report_;
  std::string output_path_;
};
} // namespace Envoy
