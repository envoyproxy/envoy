#pragma once

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {

struct DefaultsProfile {
  struct Cluster {
    uint32_t max_buffer_size = 32768;
  };

  struct Http2 {
    uint32_t initial_connection_window_size = 1048576; // 1 MiB
    uint32_t max_concurrent_streams = 100;
    uint32_t initial_stream_window_size = 65536; // 64 KiB
  };

  struct HttpConnectionManager {
    bool merge_slashes = true;
#ifdef ENVOY_NORMALIZE_PATH_BY_DEFAULT
    bool normalize_path = true;
#else
    bool normalize_path = false;
#endif
    uint64_t request_headers_timeout{10 * 1000};
    uint64_t stream_idle_timeout{60 * 1000};
    bool use_remote_address = true;
  };

  struct Listener {
    uint32_t max_buffer_size = 32768;
  };

  struct Transport {
    uint64_t transport_socket_connect_timeout{10 * 1000};
  };

  static const DefaultsProfile& get();

  Cluster cluster;
  Http2 http2;
  HttpConnectionManager httpConnectionManager;
  Listener listener;
  Transport transport;
};

using DefaultsProfileSingleton = InjectableSingleton<DefaultsProfile>;
using ScopedDefaultsProfileSingleton = ScopedInjectableLoader<DefaultsProfile>;

const DefaultsProfile& getDefaultsProfile();

} // namespace Envoy
