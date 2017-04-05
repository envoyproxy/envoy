#include "test/tools/router_check/source/router.h"

Json::ObjectPtr RouterCheckTool::loadJson(const std::string& config_json,
                                          const std::string& schema) {

  try {
    // Json configuration
    Json::ObjectPtr loader = Json::Factory::LoadFromFile(config_json);
    loader->validateSchema(schema);
    return loader;

  } catch (const EnvoyException& ex) {
    std::cerr << "config schema JSON load failed: " << config_json << std::endl;
    std::cerr << ex.what() << std::endl;
    return nullptr;
  }
}

bool RouterCheckTool::create(const std::string& router_config_json) {

  Json::ObjectPtr loader = loadJson(router_config_json, Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  if (loader != nullptr) {
    config_.reset(new Router::ConfigImpl(*loader, runtime_, cm_, false));
    return true;
  }
  return false;
}

bool RouterCheckTool::compareEntriesInJson(const std::string& expected_route_json) {
  // Load tool config json
  Json::ObjectPtr loader = loadJson(expected_route_json, Json::ToolSchema::ROUTER_CHECK_SCHEMA);

  if (loader == nullptr) {
    return false;
  }

  bool no_failures = true;
  // Iterate through each test case
  for (const Json::ObjectPtr& check_config : loader->asObjectArray()) {
    // Load parameters from json
    ToolConfig tool_config;
    tool_config.parseFromJson(check_config);

    Json::ObjectPtr check_type = check_config->getObject("check");

    // Call appropriate function for each match case
    if (check_type->hasObject("path_redirect")) {
      if (!tool_config.set_ssl_) {
        // Default to http if not specfied in tool config json
        tool_config.headers_.addViaCopy("x-forwarded-proto", "http");
      }

      if (!compareRedirectPath(tool_config, check_type->getString("path_redirect"))) {
        no_failures = false;
      }
    }

    if (!tool_config.set_method_) {
      // Default to GET if not specified in tool config json
      tool_config.headers_.addViaCopy(":method", "GET");
    }

    if (check_type->hasObject("cluster_name")) {
      if (!compareCluster(tool_config, check_type->getString("cluster_name"))) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("virtual_cluster_name")) {
      if (!compareVirtualCluster(tool_config, check_type->getString("virtual_cluster_name"))) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("virtual_host_name")) {
      if (!compareVirtualHost(tool_config, check_type->getString("virtual_host_name"))) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("path_rewrite")) {
      if (!compareRewritePath(tool_config, check_type->getString("path_rewrite"))) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("host_rewrite")) {
      if (!compareRewriteHost(tool_config, check_type->getString("host_rewrite"))) {
        no_failures = false;
      }
    }
  }

  return no_failures;
}

void ToolConfig::parseFromJson(const Json::ObjectPtr& check_config) {
  // Extract values from json object
  authority_ = check_config->getString("authority");
  path_ = check_config->getString("path");
  random_lb_value_ = check_config->getInteger("random_lb_value", 0);
  internal_ = check_config->getBoolean("internal", false);

  // Add :method to header and set flag
  if (check_config->hasObject("method")) {
    headers_.addViaCopy(":method", check_config->getString("method"));
    set_method_ = true;
  }

  if (check_config->hasObject("ssl")) {
    headers_.addViaCopy("x-forwarded-proto", check_config->getBoolean("ssl") ? "https" : "http");
    set_ssl_ = true;
  }

  // Add authority and path to header
  headers_.addViaCopy(":authority", authority_);
  headers_.addViaCopy(":path", path_);

  // Add internal route status to header
  if (internal_) {
    headers_.addViaCopy("x-envoy-internal", "true");
  }

  // Add additional headers
  if (check_config->hasObject("additional_headers")) {
    for (const Json::ObjectPtr& header_config :
         check_config->getObjectArray("additional_headers")) {
      headers_.addViaCopy(header_config->getString("name"), header_config->getString("value"));
    }
  }
}

bool RouterCheckTool::compareCluster(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  // Compare cluster name match
  if (route != nullptr && route->routeEntry() != nullptr) {
    actual = route->routeEntry()->clusterName();
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareVirtualCluster(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    if (route->routeEntry()->virtualCluster(tool_config.headers_)) {
      actual = route->routeEntry()->virtualCluster(tool_config.headers_)->name();
    }
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareVirtualHost(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    actual = route->routeEntry()->virtualHost().name();
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareRewritePath(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Path);
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareRewriteHost(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Host);
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareRedirectPath(ToolConfig& tool_config, const std::string expected) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);
  std::string actual = "none";

  if (route != nullptr && route->redirectEntry() != nullptr) {
    actual = route->redirectEntry()->newPath(tool_config.headers_);
  }

  return compareResults(actual, expected);
}

bool RouterCheckTool::compareResults(const std::string& actual, const std::string& expected) {
  if (expected == actual || (actual.empty() && expected == "none")) {
    // Output pass stdout if details is set to true
    if (details_) {
      std::cout << "P -----" << expected << " ----- " << actual << std::endl;
    }
    return true;
  }

  // Output failure case to stdout if details is set to true
  if (details_) {
    std::cout << "F ----- " << expected << " ----- " << actual << std::endl;
  }
  return false;
}
