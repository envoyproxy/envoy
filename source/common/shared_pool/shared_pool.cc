#include "common/shared_pool/shared_pool.h"

namespace Envoy {

SINGLETON_MANAGER_REGISTRATION(const_metadata_shared_pool);

ConstMetadataSharedPoolSharedPtr getConstMetadataSharedPool(Singleton::Manager& manager) {
  return manager.getTyped<ObjectSharedPool<const envoy::config::core::v3::Metadata, MessageUtil>>(
      SINGLETON_MANAGER_REGISTERED_NAME(const_metadata_shared_pool), [] {
        return std::make_shared<
            ObjectSharedPool<const envoy::config::core::v3::Metadata, MessageUtil>>();
      });
}

} // namespace Envoy