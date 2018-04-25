#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

/**
 * Well-known listener filter names.
 * NOTE: New filters should use the well known name: envoy.filters.listener.name.
 */
class ListenerFilterNameValues {
public:
  // Original destination listener filter
  const std::string ORIGINAL_DST = "envoy.listener.original_dst";
  // Proxy Protocol listener filter
  const std::string PROXY_PROTOCOL = "envoy.listener.proxy_protocol";
  // TLS Inspector listener filter
  const std::string TLS_INSPECTOR = "envoy.listener.tls_inspector";
};

typedef ConstSingleton<ListenerFilterNameValues> ListenerFilterNames;

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
