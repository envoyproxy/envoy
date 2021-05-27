#pragma once

#include <string>

#include "include/envoy/common/exception.h"
#include "include/envoy/http/codes.h"
#include "include/envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Indicates a client (local) side error which should not happen.
 */
class CodecClientException : public EnvoyException {
public:
  CodecClientException(const std::string& message) : EnvoyException(message) {}
};

} // namespace Http
} // namespace Envoy
