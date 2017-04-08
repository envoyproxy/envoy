#pragma once

#include "envoy/json/json_object.h"

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

} // Json
