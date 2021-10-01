#pragma once

#include "source/common/upstream/logical_dns_cluster.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "source/extensions/compression/gzip/decompressor/config.h"
#include "source/extensions/filters/http/buffer/config.h"
#include "source/extensions/filters/http/decompressor/config.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"
#include "source/extensions/filters/http/router/config.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"
#include "source/extensions/http/original_ip_detection/xff/config.h"
#include "source/extensions/stat_sinks/metrics_service/config.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/config.h"
#include "source/extensions/upstreams/http/generic/config.h"

#include "library/common/extensions/filters/http/assertion/config.h"
#include "library/common/extensions/filters/http/local_error/config.h"
#include "library/common/extensions/filters/http/network_configuration/config.h"
#include "library/common/extensions/filters/http/platform_bridge/config.h"
#include "library/common/extensions/filters/http/route_cache_reset/config.h"
#include "library/common/extensions/retry/options/network_configuration/config.h"

namespace Envoy {
class ExtensionRegistry {
public:
  // As a server, Envoy's static factory registration happens when main is run. However, when
  // compiled as a library, there is no guarantee that such registration will happen before the
  // names are needed. The following calls ensure that registration happens before the entities are
  // needed. Note that as more registrations are needed, explicit initialization calls will need to
  // be added here.
  static void registerFactories();
};
} // namespace Envoy
