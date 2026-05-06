#include "test/mocks/config/eds_resources_cache.h"

namespace Envoy {
namespace Config {
using testing::_;
using testing::Return;

MockEdsResourcesCache::MockEdsResourcesCache() {
  ON_CALL(*this, getResource(_, _)).WillByDefault(Return(absl::nullopt));
}

} // namespace Config
} // namespace Envoy
