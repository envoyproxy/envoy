#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/http/codes.h"
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
 * Raised when outbound frame queue flood is detected.
 */
class FrameFloodException : public CodecProtocolException {
public:
  FrameFloodException(const std::string& message) : CodecProtocolException(message) {}
};

/**
 * Raised when a response is received on a connection that did not send a request. In practice
 * this can only happen on HTTP/1.1 connections.
 */
class PrematureResponseException : public EnvoyException {
public:
  PrematureResponseException(Http::Code response_code)
      : EnvoyException(""), response_code_(response_code) {}

  Http::Code responseCode() { return response_code_; }

private:
  const Http::Code response_code_;
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
