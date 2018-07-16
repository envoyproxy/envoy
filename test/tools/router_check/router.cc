#include "test/tools/router_check/router.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "common/network/utility.h"
#include "common/request_info/request_info_impl.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/printers.h"

namespace Envoy {
// static
ToolConfig ToolConfig::create(const Json::ObjectSharedPtr check_config) {
  Json::ObjectSharedPtr input = check_config->getObject("input");
  int random_value = input->getInteger("random_value", 0);

  // Add header field values
  std::unique_ptr<Http::TestHeaderMapImpl> headers(new Http::TestHeaderMapImpl());
  headers->addCopy(":authority", input->getString(":authority", ""));
  headers->addCopy(":path", input->getString(":path", ""));
  headers->addCopy(":method", input->getString(":method", "GET"));
  headers->addCopy("x-forwarded-proto", input->getBoolean("ssl", false) ? "https" : "http");

  if (input->getBoolean("internal", false)) {
    headers->addCopy("x-envoy-internal", "true");
  }

  if (input->hasObject("additional_headers")) {
    for (const Json::ObjectSharedPtr& header_config : input->getObjectArray("additional_headers")) {
      headers->addCopy(header_config->getString("field"), header_config->getString("value"));
    }
  }

  return ToolConfig(std::move(headers), random_value);
}

ToolConfig::ToolConfig(std::unique_ptr<Http::TestHeaderMapImpl> headers, int random_value)
    : headers_(std::move(headers)), random_value_(random_value) {}

// static
RouterCheckTool RouterCheckTool::create(const std::string& router_config_json) {
  // TODO(hennna): Allow users to load a full config and extract the route configuration from it.
  Json::ObjectSharedPtr loader = Json::Factory::loadFromFile(router_config_json);
  envoy::api::v2::RouteConfiguration route_config;
  // TODO(ambuc): Add a CLI option to allow for a maxStatNameLength constraint
  Stats::StatsOptionsImpl stats_options;
  Config::RdsJson::translateRouteConfiguration(*loader, route_config, stats_options);

  std::unique_ptr<NiceMock<Server::Configuration::MockFactoryContext>> factory_context(
      std::make_unique<NiceMock<Server::Configuration::MockFactoryContext>>());
  std::unique_ptr<Router::ConfigImpl> config(
      new Router::ConfigImpl(route_config, *factory_context, false));

  return RouterCheckTool(std::move(factory_context), std::move(config));
}

RouterCheckTool::RouterCheckTool(
    std::unique_ptr<NiceMock<Server::Configuration::MockFactoryContext>> factory_context,
    std::unique_ptr<Router::ConfigImpl> config)
    : factory_context_(std::move(factory_context)), config_(std::move(config)) {}

bool RouterCheckTool::compareEntriesInJson(const std::string& expected_route_json) {
  Json::ObjectSharedPtr loader = Json::Factory::loadFromFile(expected_route_json);
  loader->validateSchema(Json::ToolSchema::routerCheckSchema());

  bool no_failures = true;
  for (const Json::ObjectSharedPtr& check_config : loader->asObjectArray()) {
    ToolConfig tool_config = ToolConfig::create(check_config);
    tool_config.route_ = config_->route(*tool_config.headers_, tool_config.random_value_);

    std::string test_name = check_config->getString("test_name", "");
    if (details_) {
      std::cout << test_name << std::endl;
    }
    Json::ObjectSharedPtr validate = check_config->getObject("validate");

    const std::unordered_map<std::string, std::function<bool(ToolConfig&, const std::string&)>>
        checkers = {
            {"cluster_name",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareCluster(tool_config, expected);
             }},
            {"virtual_cluster_name",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareVirtualCluster(tool_config, expected);
             }},
            {"virtual_host_name",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareVirtualHost(tool_config, expected);
             }},
            {"path_rewrite",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareRewritePath(tool_config, expected);
             }},
            {"host_rewrite",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareRewriteHost(tool_config, expected);
             }},
            {"path_redirect",
             [this](ToolConfig& tool_config, const std::string& expected) -> bool {
               return compareRedirectPath(tool_config, expected);
             }},
        };

    // Call appropriate function for each match case
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
      for (const Json::ObjectSharedPtr& header_field : validate->getObjectArray("header_fields")) {
        if (!compareHeaderField(tool_config, header_field->getString("field"),
                                header_field->getString("value"))) {
          no_failures = false;
        }
      }
    }

    if (validate->hasObject("custom_header_fields")) {
      for (const Json::ObjectSharedPtr& header_field :
           validate->getObjectArray("custom_header_fields")) {
        if (!compareCustomHeaderField(tool_config, header_field->getString("field"),
                                      header_field->getString("value"))) {
          no_failures = false;
        }
      }
    }
  }
  return no_failures;
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
      tool_config.route_->routeEntry()->virtualCluster(*tool_config.headers_) != nullptr) {
    actual = tool_config.route_->routeEntry()->virtualCluster(*tool_config.headers_)->name();
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
  Envoy::RequestInfo::RequestInfoImpl request_info(Envoy::Http::Protocol::Http11);
  if (tool_config.route_->routeEntry() != nullptr) {
    tool_config.route_->routeEntry()->finalizeRequestHeaders(*tool_config.headers_, request_info,
                                                             true);
    actual = tool_config.headers_->get_(Http::Headers::get().Path);
  }
  return compareResults(actual, expected, "path_rewrite");
}

bool RouterCheckTool::compareRewriteHost(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";
  Envoy::RequestInfo::RequestInfoImpl request_info(Envoy::Http::Protocol::Http11);
  if (tool_config.route_->routeEntry() != nullptr) {
    tool_config.route_->routeEntry()->finalizeRequestHeaders(*tool_config.headers_, request_info,
                                                             true);
    actual = tool_config.headers_->get_(Http::Headers::get().Host);
  }
  return compareResults(actual, expected, "host_rewrite");
}

bool RouterCheckTool::compareRedirectPath(ToolConfig& tool_config, const std::string& expected) {
  std::string actual = "";
  if (tool_config.route_->directResponseEntry() != nullptr) {
    actual = tool_config.route_->directResponseEntry()->newPath(*tool_config.headers_);
  }

  return compareResults(actual, expected, "path_redirect");
}

bool RouterCheckTool::compareHeaderField(ToolConfig& tool_config, const std::string& field,
                                         const std::string& expected) {
  std::string actual = tool_config.headers_->get_(field);
  return compareResults(actual, expected, "check_header");
}

bool RouterCheckTool::compareCustomHeaderField(ToolConfig& tool_config, const std::string& field,
                                               const std::string& expected) {
  std::string actual = "";
  Envoy::RequestInfo::RequestInfoImpl request_info(Envoy::Http::Protocol::Http11);
  request_info.setDownstreamRemoteAddress(Network::Utility::getCanonicalIpv4LoopbackAddress());
  if (tool_config.route_->routeEntry() != nullptr) {
    tool_config.route_->routeEntry()->finalizeRequestHeaders(*tool_config.headers_, request_info,
                                                             true);
    actual = tool_config.headers_->get_(field);
  }
  return compareResults(actual, expected, "custom_header");
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
} // namespace Envoy
