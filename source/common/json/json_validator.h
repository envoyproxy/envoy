#pragma once

#include "envoy/json/json_object.h"

namespace Json {

class JsonValidator {
public:
  /*
   * Base class to inherit from to validate config schema before initializing member variables.
   */
  JsonValidator(const Json::Object& config, const std::string schema) {
    config.validateSchema(schema);
  }
};

} // Json
