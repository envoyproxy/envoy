#pragma once

#include <fstream>

#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/precompiled/precompiled_test.h"
#include "test/test_common/utility.h"
#include "test/tools/json/tool_config_schemas.h"

/** Class that store the configuration parameters of the router
 * check tool extracted from a json input file
 */
class ToolConfig {
public:
  ToolConfig(){};
  void parseFromJson(const Json::ObjectPtr& check_config);

  bool ssl_;
  bool internal_;
  int random_lb_value_;

  Http::TestHeaderMapImpl headers_;
  std::string actual_ = "none";
  std::string authority_;
  std::string expected_;
  std::string method_;
  std::string path_;
  std::string type_;
};

/**
 * Router check tool to check routes returned by a router
 */
class RouterCheckTool : Logger::Loggable<Logger::Id::testing> {
public:
  RouterCheckTool(){};

  /**
   * @param filename_json specifies the router config file to be loaded
   * @return bool true if router config loaded successfully
   */
  bool create(const std::string& filename_json);

  /**
   * @param filename_json refers to the filename with the expected router
   * config entries to be compared
   */
  void compareEntriesInJson(const std::string& filename_json);

  /**
   * Get the number of matches
   * @return int total number of matches
   */
  int getYes() { return match_yes_; }

  /**
   * Get the number of conflicts
   * @return int total number of conflicts
   */
  int getNo() { return match_no_; }

  /* Set whether to print out match case details
   * @ param
   */
  void showDetails() { details_ = true; }

private:
  /**
   * Compare whether cluster name matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareCluster(ToolConfig& tool_config);

  /**
   * Compare whether virtual cluster name matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareVirtualCluster(ToolConfig& tool_config);

  /**
   * Compare whether vituaal host name matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareVirtualHost(ToolConfig& tool_config);

  /**
   * Compare whether rewritten host matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareRewriteHost(ToolConfig& tool_config);

  /**
   * Compare whether rewritten path matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareRewritePath(ToolConfig& tool_config);

  /**
   * Compare whether redirect path matches
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void compareRedirectPath(ToolConfig& tool_config);

  /**
   * Increase yes counter if route matches and increases no counter otherwise
   * @param tool_config holds the configuration parameters extracted from a
   * json input file
   */
  void increaseCounters(ToolConfig& tool_config);

  bool details_ = false;
  int match_yes_ = 0;
  int match_no_ = 0;

  // TODO(hennna): Switch away from mocks depending on feedback
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Router::ConfigImpl* config_;
};
