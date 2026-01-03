#pragma once

#include "envoy/stats/scope.h"

#include "source/common/tls/stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Interface for providing SSL stats from SSL_CTX app_data.
// This avoids circular dependency between cert_compression_lib and context_lib.
class SslCtxStatsProvider {
public:
  virtual ~SslCtxStatsProvider() = default;
  virtual SslStats& stats() = 0;
  virtual Stats::Scope& statsScope() = 0;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
