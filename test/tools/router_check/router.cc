#include "test/tools/router_check/router.h"

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

bool RouterCheckTool::initializeFromConfig(const std::string& router_config_json) {

  Json::ObjectPtr loader = loadJson(router_config_json, Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  if (loader != nullptr) {
    config_.reset(new Router::ConfigImpl(*loader, runtime_, cm_, false));
    return true;
  }
  return false;
}

bool RouterCheckTool::compareEntriesInJson(const std::string& expected_route_json) {
  // Load tool config json
  Json::ObjectPtr loader = loadJson(expected_route_json, Json::ToolSchema::routerCheckSchema());

  if (loader == nullptr) {
    return false;
  }

  bool no_failures = true;
  // Iterate through each test case
  for (const Json::ObjectPtr& check_config : loader->asObjectArray()) {
    // Load parameters from json
    ToolConfig tool_config;
    tool_config.parseFromJson(check_config);
    Router::RouteConstSharedPtr route =
        config_->route(tool_config.headers_, tool_config.random_value_);

    Json::ObjectPtr check_type = check_config->getObject("check");

    // Call appropriate function for each match case
    if (check_type->hasObject("path_redirect")) {
      if (!compareRedirectPath(tool_config, check_type->getString("path_redirect"), route)) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("cluster_name")) {
      if (!compareCluster(tool_config, check_type->getString("cluster_name"), route)) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("virtual_cluster_name")) {
      if (!compareVirtualCluster(tool_config, check_type->getString("virtual_cluster_name"),
                                 route)) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("virtual_host_name")) {
      if (!compareVirtualHost(tool_config, check_type->getString("virtual_host_name"), route)) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("path_rewrite")) {
      if (!compareRewritePath(tool_config, check_type->getString("path_rewrite"), route)) {
        no_failures = false;
      }
    }

    if (check_type->hasObject("host_rewrite")) {
      if (!compareRewriteHost(tool_config, check_type->getString("host_rewrite"), route)) {
        no_failures = false;
      }
    }
  }

  return no_failures;
}

void ToolConfig::parseFromJson(const Json::ObjectPtr& check_config) {
  // Extract values from json object
  bool internal = check_config->getBoolean("internal", false);
  bool ssl = check_config->getBoolean("ssl", false);
  std::string authority = check_config->getString("authority");
  std::string method = check_config->getString("method", "GET");
  std::string path = check_config->getString("path");

  random_value_ = check_config->getInteger("random_value", 0);

  // Add :method to header
  headers_.addViaCopy(":method", method);

  // Add ssl header
  headers_.addViaCopy("x-forwarded-proto", ssl ? "https" : "http");

  // Add authority and path to header
  headers_.addViaCopy(":authority", authority);
  headers_.addViaCopy(":path", path);

  // Add internal route status to header
  if (internal) {
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

bool RouterCheckTool::compareCluster(ToolConfig& tool_config, const std::string expected,
                                     Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  // Compare cluster name match
  if (route != nullptr && route->routeEntry() != nullptr) {
    actual = route->routeEntry()->clusterName();
  }

  return compareResults(actual, expected, "cluster_name");
}

bool RouterCheckTool::compareVirtualCluster(ToolConfig& tool_config, const std::string expected,
                                            Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    if (route->routeEntry()->virtualCluster(tool_config.headers_)) {
      actual = route->routeEntry()->virtualCluster(tool_config.headers_)->name();
    }
  }

  return compareResults(actual, expected, "virtual_cluster_name");
}

bool RouterCheckTool::compareVirtualHost(ToolConfig& tool_config, const std::string expected,
                                         Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    actual = route->routeEntry()->virtualHost().name();
  }

  return compareResults(actual, expected, "virtual_host_name");
}

bool RouterCheckTool::compareRewritePath(ToolConfig& tool_config, const std::string expected,
                                         Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Path);
  }

  return compareResults(actual, expected, "path_rewrite");
}

bool RouterCheckTool::compareRewriteHost(ToolConfig& tool_config, const std::string expected,
                                         Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  if (route != nullptr && route->routeEntry() != nullptr) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Host);
  }

  return compareResults(actual, expected, "host_rewrite");
}

bool RouterCheckTool::compareRedirectPath(ToolConfig& tool_config, const std::string expected,
                                          Router::RouteConstSharedPtr& route) {
  std::string actual = "none";

  if (route != nullptr && route->redirectEntry() != nullptr) {
    actual = route->redirectEntry()->newPath(tool_config.headers_);
  }

  return compareResults(actual, expected, "path_redirect");
}

bool RouterCheckTool::compareResults(const std::string& actual, const std::string& expected,
                                     const std::string& test_type) {
  if (expected == actual || (actual.empty() && expected == "none")) {
    // Output pass details to stdout if details_ flag is set to true
    if (details_) {
      std::cout << "P " << expected << " " << actual << " " << test_type << std::endl;
    }
    return true;
  }

  // Output failure details to stdout if details_ flag is set to true
  if (details_) {
    std::cout << "F " << expected << " " << actual << " " << test_type << std::endl;
  }
  return false;
}
