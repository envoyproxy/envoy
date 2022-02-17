#pragma once

#include <string>

#include "source/common/common/macros.h"

namespace Envoy {
namespace Http {

class DefaultServerString {
public:
  /**
   * @return the default HTTP server header string.
   */
  static const std::string& get() { CONSTRUCT_ON_FIRST_USE(std::string, "envoy"); }
};

} // namespace Http
} // namespace Envoy
