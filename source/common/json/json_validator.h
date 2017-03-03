#pragma once

#include "envoy/json/json_object.h"

namespace Json {

class JsonValidator {
public:
  /*
   * Base class to inherit from when validating a config schema.
   */
  JsonValidator(const Json::Object& config, const std::string schema) {
    config.validateSchema(schema);
  }
};

} // Json
