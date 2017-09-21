#pragma once

#include <memory>
#include <string>

#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"
#include "test/tools/router_check/json/tool_config_schemas.h"

namespace Envoy {
/**
 * Struct that stores the configuration parameters of the router check tool extracted from a json
 * input file.
 */
struct ToolConfig {
  ToolConfig() : random_value_(0){};

  /**
   * @param check_config tool config json object pointer.
   * @return ToolConfig a ToolConfig instance with member variables set by the tool config json
   * file.
   */
  static ToolConfig create(const Json::ObjectSharedPtr check_config);

  std::unique_ptr<Http::TestHeaderMapImpl> headers_;
  Router::RouteConstSharedPtr route_;
  int random_value_;

private:
  ToolConfig(std::unique_ptr<Http::TestHeaderMapImpl> headers, int random_value);
};

/**
 * A route table check tool that check whether route parameters returned by a router match
 * what is expected.
 */
class RouterCheckTool : Logger::Loggable<Logger::Id::testing> {
public:
  /**
   * @param router_config_json router config json file.
   * @return RouterCheckTool a RouterCheckTool instance with member variables set by the router
   * config json file.
   * */
  static RouterCheckTool create(const std::string& router_config_json);

  /**
   * @param expected_route_json tool config json file.
   * @return bool if all routes match what is expected.
   */
  bool compareEntriesInJson(const std::string& expected_route_json);

  /**
   * Set whether to print out match case details.
   */
  void setShowDetails() { details_ = true; }

private:
  RouterCheckTool(std::unique_ptr<NiceMock<Runtime::MockLoader>> runtime,
                  std::unique_ptr<NiceMock<Upstream::MockClusterManager>> cm,
                  std::unique_ptr<Router::ConfigImpl> config);
  bool compareCluster(ToolConfig& tool_config, const std::string& expected);
  bool compareVirtualCluster(ToolConfig& tool_config, const std::string& expected);
  bool compareVirtualHost(ToolConfig& tool_config, const std::string& expected);
  bool compareRewriteHost(ToolConfig& tool_config, const std::string& expected);
  bool compareRewritePath(ToolConfig& tool_config, const std::string& expected);
  bool compareRedirectPath(ToolConfig& tool_config, const std::string& expected);
  bool compareHeaderField(ToolConfig& tool_config, const std::string& field,
                          const std::string& expected);
  bool compareCustomHeaderField(ToolConfig& tool_config, const std::string& field,
                                const std::string& expected);
  /**
   * Compare the expected and acutal route parameter values. Print out match details if details_
   * flag is set.
   * @param actual holds the actual route returned by the router.
   * @param expected holds the expected parameter value of the route.
   * @return bool if actual and expected match.
   */
  bool compareResults(const std::string& actual, const std::string& expected,
                      const std::string& test_type);

  bool details_{false};

  // TODO(hennna): Switch away from mocks following work done by @rlazarus in github issue #499.
  std::unique_ptr<NiceMock<Runtime::MockLoader>> runtime_;
  std::unique_ptr<NiceMock<Upstream::MockClusterManager>> cm_;
  std::unique_ptr<Router::ConfigImpl> config_;
};
} // namespace Envoy
