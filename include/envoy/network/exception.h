#pragma once

#include "envoy/common/exception.h"

namespace Envoy {
namespace Network {

/**
 * Thrown when there is a runtime error creating/binding a listener.
 */
class CreateListenerException : public EnvoyException {
public:
  CreateListenerException(const std::string& what) : EnvoyException(what) {}
};

/**
 * Thrown when there is a runtime error binding a socket.
 */
class SocketBindException : public CreateListenerException {
public:
  SocketBindException(const std::string& what, int error_number)
      : CreateListenerException(what), error_number_(error_number) {}

  // This can't be called errno because otherwise the standard errno macro expansion replaces it.
  int errorNumber() const { return error_number_; }

private:
  const int error_number_;
};

} // namespace Network
} // namespace Envoy