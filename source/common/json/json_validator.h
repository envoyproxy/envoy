#pragma once

#include <string>

#include "envoy/json/json_object.h"

namespace Envoy {
namespace Json {

/**
 * Base class to inherit from to validate config schema before initializing member variables.
 */
class Validator {
public:
  Validator(const Json::Object& config, const std::string& schema) {
    config.validateSchema(schema);
  }
};

} // namespace Json
} // namespace Envoy
