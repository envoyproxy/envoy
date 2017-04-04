#include "router.h"

bool RouterCheckTool::create(const std::string& filename_json) {
  // Throws expection if json file not correctly written
  Json::ObjectPtr loader = Json::Factory::LoadFromFile(filename_json);
  if (loader->empty()) {
    log_debug("JSON file load failed.");
    return false;
  }

  // Throws expection if json file not correctly written
  loader->validateSchema(Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  // Router configuration
  config_ = new Router::ConfigImpl(*loader, runtime_, cm_, false);
  return true;
}

void RouterCheckTool::compareEntriesInJson(const std::string& filename_json) {
  // Throws expection if json file not correctly written
  Json::ObjectPtr loader = Json::Factory::LoadFromFile(filename_json);
  if (loader->empty()) {
    log_debug("JSON file load failed.");
    return;
  }

  // Throws expection if json file not correctly written
  loader->validateSchema(Json::ToolSchema::ROUTER_CHECK_SCHEMA);
  if (details_) {
    // Print header for detailed match information
    std::cout << "  Expected  Actual" << std::endl;
    std::cout << "  ========  ======" << std::endl;
  }
  // Iterate through each expected and acutal route match
  for (const Json::ObjectPtr& check_config : loader->getObjectArray("input")) {
    // Load parameters from JSON
    ToolConfig tool_config;
    tool_config.parseFromJson(check_config);

    // Call appropriate match case
    if (tool_config.type_ == "name_cluster") {
      compareCluster(tool_config);
    } else if (tool_config.type_ == "name_virtual_host") {
      compareVirtualHost(tool_config);
    } else if (tool_config.type_ == "name_virtual_cluster") {
      compareVirtualCluster(tool_config);
    } else if (tool_config.type_ == "rewrite_path") {
      compareRewritePath(tool_config);
    } else if (tool_config.type_ == "rewrite_host") {
      compareRewriteHost(tool_config);
    } else if (tool_config.type_ == "redirect_path") {
      compareRedirectPath(tool_config);
    }

    // Print details of each match case
    if (details_) {
      std::cout << tool_config.expected_ << " " << tool_config.actual_ << std::endl;
    }
  }
}

void ToolConfig::parseFromJson(const Json::ObjectPtr& check_config) {
  // Extract values from json object
  bool redirect = false;
  expected_ = check_config->getString("expected");
  authority_ = check_config->getString("authority");
  path_ = check_config->getString("path");
  method_ = check_config->getString("method", "GET");
  random_lb_value_ = check_config->getInteger("random_lb_value", 0);
  internal_ = check_config->getBoolean("internal", false);
  ssl_ = check_config->getBoolean("ssl", false);

  // Parse the test type
  type_ = "name_cluster";
  if (check_config->hasObject("check")) {
    Json::ObjectPtr check_type = check_config->getObject("check");
    if (check_type->hasObject("name")) {
      type_ = "name_" + check_type->getString("name");
    } else if (check_type->hasObject("rewrite")) {
      type_ = "rewrite_" + check_type->getString("rewrite");
    } else if (check_type->hasObject("redirect")) {
      type_ = "redirect_" + check_type->getString("redirect");
      redirect = true;
    }
  }

  // Add authority and path to http header
  headers_.addViaCopy(":authority", authority_);
  headers_.addViaCopy(":path", path_);

  // Add method to header
  if (!redirect) {
    headers_.addViaCopy(":method", method_);
  }

  // Add ssl to header
  if (redirect || ssl_) {
    headers_.addViaCopy("x-forwarded-proto", ssl_ ? "https" : "http");
  }

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

void RouterCheckTool::compareCluster(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare cluster name match
  if (route && route->routeEntry()) {
    tool_config.actual_ = route->routeEntry()->clusterName();
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::compareVirtualCluster(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare virtual cluster name match
  if (route && route->routeEntry()) {
    if (route->routeEntry()->virtualCluster(tool_config.headers_)) {
      tool_config.actual_ = route->routeEntry()->virtualCluster(tool_config.headers_)->name();
    }
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::compareVirtualHost(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare virutal host name match
  if (route && route->routeEntry()) {
    tool_config.actual_ = route->routeEntry()->virtualHost().name();
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::compareRewritePath(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare rewrite path match
  if (route && route->routeEntry()) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    tool_config.actual_ = tool_config.headers_.get_(Http::Headers::get().Path);
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::compareRewriteHost(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare rewrite host match
  if (route && route->routeEntry()) {
    route->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    tool_config.actual_ = tool_config.headers_.get_(Http::Headers::get().Host);
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::compareRedirectPath(ToolConfig& tool_config) {
  Router::RouteConstSharedPtr route =
      config_->route(tool_config.headers_, tool_config.random_lb_value_);

  // Compare redirect path match
  if (route && route->redirectEntry()) {
    tool_config.actual_ = route->redirectEntry()->newPath(tool_config.headers_);
  }

  increaseCounters(tool_config);
}

void RouterCheckTool::increaseCounters(ToolConfig& tool_config) {
  // Update match and conflict counters
  // Output to stdout if details is set to true
  if (tool_config.expected_ == tool_config.actual_ ||
      (tool_config.actual_.empty() && tool_config.expected_ == "none")) {
    ++match_yes_;
    if (details_) {
      std::cout << "Y ";
    }
  } else {
    ++match_no_;
    if (details_) {
      std::cout << "N ";
    }
  }
}
