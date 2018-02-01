#pragma once
#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

/**
 * Possible HTTP connection/request protocols. The parallel NumProtocols constant allows defining
 * fixed arrays for each protocol, but does not pollute the enum.
 */
enum class Protocol { Http10, Http11, Http2 };
const size_t NumProtocols = 3;

class ProtocolVal {
public:
  struct {
    const std::string Http10String{"HTTP/1.0"};
    const std::string Http11String{"HTTP/1.1"};
    const std::string Http2String{"HTTP/2"};
  } ProtocolStrings;
};

typedef ConstSingleton<ProtocolVal> ProtocolValues;

inline const std::string& getProtocolString(const Protocol p) {
  switch (p) {
  case Protocol::Http10:
    return ProtocolValues::get().ProtocolStrings.Http10String;
  case Protocol::Http11:
    return ProtocolValues::get().ProtocolStrings.Http11String;
  case Protocol::Http2:
    return ProtocolValues::get().ProtocolStrings.Http2String;
  }

  // equivalet of NOT_REACHED,
  // which we cannot use here as that causes a circular dependency.
  abort();
}

} // namespace Http
} // namespace Envoy
