#pragma once

#include "envoy/common/exception.h"

namespace Envoy {
namespace Network {

/**
 * Thrown when a socket option cannot be applied.
 */
class SocketOptionException : public EnvoyException {
public:
  SocketOptionException(const std::string& what) : EnvoyException(what) {}
};

/**
 * Thrown when there is a runtime error binding a socket.
 */
class SocketBindException : public EnvoyException {
public:
  SocketBindException(const std::string& what, int error_number)
      : EnvoyException(what), error_number_(error_number) {}

  // This can't be called errno because otherwise the standard errno macro expansion replaces it.
  int errorNumber() const { return error_number_; }

private:
  const int error_number_;
};

} // namespace Network
} // namespace Envoy
