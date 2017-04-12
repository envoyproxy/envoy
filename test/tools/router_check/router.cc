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
  // TODO(hennna): Allow users to load a full config and extract the route
  // configuration from it.
  Json::ObjectPtr loader = loadJson(router_config_json, Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  if (loader != nullptr) {
    config_.reset(new Router::ConfigImpl(*loader, runtime_, cm_, false));
    return true;
  }
  return false;
}

bool RouterCheckTool::compareEntriesInJson(const std::string& expected_route_json) {
  if (config_ == nullptr) {
    return false;
  }

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
    tool_config.route_ = config_->route(tool_config.headers_, tool_config.random_value_);

    std::string test_name = check_config->getString("test_name", "");
    if (details_) {
      std::cout << test_name << std::endl;
    }
    Json::ObjectPtr validate = check_config->getObject("_validate");

    // Call appropriate function for each match case
    const std::unordered_map<std::string,
                             std::function<bool(ToolConfig&, const std::string&)>> checkers = {
        {"cluster_name", [this](ToolConfig& tool_config, const std::string& expected)
                             -> bool { return compareCluster(tool_config, expected); }},
        {"virtual_cluster_name",
         [this](ToolConfig& tool_config, const std::string& expected)
             -> bool { return compareVirtualCluster(tool_config, expected); }},
        {"virtual_host_name", [this](ToolConfig& tool_config, const std::string& expected)
                                  -> bool { return compareVirtualHost(tool_config, expected); }},
        {"path_rewrite", [this](ToolConfig& tool_config, const std::string& expected)
                             -> bool { return compareRewritePath(tool_config, expected); }},
        {"host_rewrite", [this](ToolConfig& tool_config, const std::string& expected)
                             -> bool { return compareRewriteHost(tool_config, expected); }},
        {"path_redirect", [this](ToolConfig& tool_config, const std::string& expected)
                              -> bool { return compareRedirectPath(tool_config, expected); }},
    };

    for (std::pair<std::string, std::function<bool(ToolConfig&, std::string)>> test : checkers) {
      if (validate->hasObject(test.first)) {
        std::string expected = validate->getString(test.first);
        if (tool_config.route_ == nullptr) {
          compareResults("", expected, test.first);
        } else {
          if (!test.second(tool_config, validate->getString(test.first))) {
            no_failures = false;
          }
        }
      }
    }

    if (validate->hasObject("header_fields")) {
      for (const Json::ObjectPtr& header_field : validate->getObjectArray("header_fields")) {
        if (!compareHeaderField(tool_config, header_field->getString("field"),
                                header_field->getString("value"))) {
          no_failures = false;
        }
      }
    }
  }
  return no_failures;
}

void ToolConfig::parseFromJson(const Json::ObjectPtr& check_config) {
  // Extract values from json object
  Json::ObjectPtr input = check_config->getObject("input");

  random_value_ = input->getInteger("random_value", 0);

  // Add authority and path to header
  headers_.addViaCopy(":authority", input->getString(":authority", ""));
  headers_.addViaCopy(":path", input->getString(":path", ""));

  // Add :method to header
  headers_.addViaCopy(":method", input->getString(":method", "GET"));

  // Add ssl header
  headers_.addViaCopy("x-forwarded-proto", input->getBoolean("ssl", false) ? "https" : "http");

  // Add internal route status to header
  if (input->getBoolean("internal", false)) {
    headers_.addViaCopy("x-envoy-internal", "true");
  }

  // Add additional headers
  if (input->hasObject("additional_headers")) {
    for (const Json::ObjectPtr& header_config : input->getObjectArray("additional_headers")) {
      headers_.addViaCopy(header_config->getString("field"), header_config->getString("value"));
    }
  }
}

bool RouterCheckTool::compareCluster(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->routeEntry() != nullptr) {
    actual = tool_config.route_->routeEntry()->clusterName();
  }
  return compareResults(actual, expected, "cluster_name");
}

bool RouterCheckTool::compareVirtualCluster(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->routeEntry() != nullptr &&
      tool_config.route_->routeEntry()->virtualCluster(tool_config.headers_) != nullptr) {
    actual = tool_config.route_->routeEntry()->virtualCluster(tool_config.headers_)->name();
  }
  return compareResults(actual, expected, "virtual_cluster_name");
}

bool RouterCheckTool::compareVirtualHost(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->routeEntry() != nullptr) {
    actual = tool_config.route_->routeEntry()->virtualHost().name();
  }
  return compareResults(actual, expected, "virtual_host_name");
}

bool RouterCheckTool::compareRewritePath(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->routeEntry() != nullptr) {
    tool_config.route_->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Path);
  }
  return compareResults(actual, expected, "path_rewrite");
}

bool RouterCheckTool::compareRewriteHost(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->routeEntry() != nullptr) {
    tool_config.route_->routeEntry()->finalizeRequestHeaders(tool_config.headers_);
    actual = tool_config.headers_.get_(Http::Headers::get().Host);
  }
  return compareResults(actual, expected, "host_rewrite");
}

bool RouterCheckTool::compareRedirectPath(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";

  if (tool_config.route_->redirectEntry() != nullptr) {
    actual = tool_config.route_->redirectEntry()->newPath(tool_config.headers_);
  }

  return compareResults(actual, expected, "path_redirect");
}

bool RouterCheckTool::compareHeaderField(ToolConfig& tool_config, const std::string& field,
                                         const std::string& expected) {
  std::string actual = tool_config.headers_.get_(field);

  return compareResults(actual, expected, "check_header");
}

bool RouterCheckTool::compareResults(const std::string& actual, const std::string& expected,
                                     const std::string& test_type) {
  if (expected == actual) {
    return true;
  }

  // Output failure details to stdout if details_ flag is set to true
  if (details_) {
    std::cout << expected << " " << actual << " " << test_type << std::endl;
  }
  return false;
}
