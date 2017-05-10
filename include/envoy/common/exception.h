#pragma once

#include <stdexcept>
#include <string>

namespace Lyft {
/**
 * Base class for all envoy exceptions.
 */
class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};
} // Lyft