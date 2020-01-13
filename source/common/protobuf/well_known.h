#pragma once

namespace Envoy {
namespace ProtobufWellKnown {

// Used by VersionConverter to track the original type of an upgraded message.
// Magic number in this file derived from top 28bit of SHA256 digest of
// "original type".
constexpr uint32_t OriginalTypeFieldNumber = 183412668;

} // namespace ProtobufWellKnown
} // namespace Envoy
