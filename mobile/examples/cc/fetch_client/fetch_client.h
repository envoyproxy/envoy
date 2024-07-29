#pragma once

#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/common/thread.h"
#include "source/common/event/real_time_system.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "library/cc/stream_prototype.h"
#include "library/common/http/client.h"

namespace Envoy {

class Fetch {
public:
  Fetch();

  /**
   * Sends requests to the specified URLs. When QUIC hints are not empty, HTTP/3 will be enabled.
   * The `protocols` output parameter will be updated upon successful fetch.
   */
  envoy_status_t fetch(const std::vector<absl::string_view>& urls,
                       const std::vector<absl::string_view>& quic_hints,
                       std::vector<Http::Protocol>& protocols);

private:
  void runEngine(absl::Notification& engine_running,
                 const std::vector<absl::string_view>& quic_hints);
  envoy_status_t sendRequest(absl::string_view url, std::vector<Http::Protocol>& protocols);

  Thread::MutexBasicLockable lock_;
  Logger::Context logging_context_;
  PlatformImpl platform_impl_;
  Stats::SymbolTableImpl symbol_table_;
  Event::RealTimeSystem time_system_; // NO_CHECK_FORMAT(real_time)
  Stats::AllocatorImpl stats_allocator_;
  Stats::ThreadLocalStoreImpl store_root_;
  Random::RandomGeneratorImpl random_generator_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  Api::ApiPtr api_;

  Event::DispatcherPtr dispatcher_;

  absl::Mutex engine_mutex_;
  Platform::EngineSharedPtr engine_ ABSL_GUARDED_BY(engine_mutex_);
};

} // namespace Envoy
