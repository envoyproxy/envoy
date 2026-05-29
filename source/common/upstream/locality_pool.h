#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/singleton/manager.h"
#include "envoy/upstream/locality.h"

#include "source/common/shared_pool/shared_pool.h"

namespace Envoy {
namespace Upstream {

using ConstLocalitySharedPool =
    SharedPool::ObjectSharedPool<const envoy::config::core::v3::Locality, LocalityHash,
                                 LocalityEqualTo>;
using ConstLocalitySharedPoolSharedPtr = std::shared_ptr<ConstLocalitySharedPool>;

class LocalityPool {
public:
  /**
   * Returns an ObjectSharedPool to store const Locality
   * @param manager used to create singleton
   * @param dispatcher the dispatcher object reference to the thread that created the
   * ObjectSharedPool
   */
  static ConstLocalitySharedPoolSharedPtr getConstLocalitySharedPool(Singleton::Manager& manager,
                                                                     Event::Dispatcher& dispatcher);
};

} // namespace Upstream
} // namespace Envoy
