#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Indicates a non-recoverable protocol error that should result in connection termination.
 */
class CodecProtocolException : public EnvoyException {
public:
  CodecProtocolException(const std::string& message) : EnvoyException(message) {}
};

/**
 * Raised when a response is received on a connection that did not send a request. In practice
 * this can only happen on HTTP/1.1 connections.
 */
class PrematureResponseException : public EnvoyException {
public:
  PrematureResponseException(HeaderMapPtr&& headers)
      : EnvoyException(""), headers_(std::move(headers)) {}

  const HeaderMap& headers() { return *headers_; }

private:
  HeaderMapPtr headers_;
};

/**
 * Indicates a client (local) side error which should not happen.
 */
class CodecClientException : public EnvoyException {
public:
  CodecClientException(const std::string& message) : EnvoyException(message) {}
};

} // namespace Http
} // namespace Envoy
