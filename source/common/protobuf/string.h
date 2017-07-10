#pragma once

namespace Envoy {
namespace Protobuf {

typedef std::string String;

inline const String ToString(const std::string& s) { return s; }

inline const std::string FromString(const String& s) { return s; }

} // namespace Protobuf
} // namespace Envoy
