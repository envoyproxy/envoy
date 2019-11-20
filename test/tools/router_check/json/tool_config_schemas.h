#pragma once

#include <string>

namespace Envoy {
namespace Json {

class ToolSchema {
public:
  /**
   * Obtain the router check json schema
   * @return const std::string& of the schema string
   */
  static const std::string& routerCheckSchema();
};

} // namespace Json
} // namespace Envoy
