#include "test/mocks/http/conn_pool.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

MockInstance::MockInstance()
    : host_{std::make_shared<testing::NiceMock<Upstream::MockHostDescription>>()} {
  ON_CALL(*this, host()).WillByDefault(Return(host_));
}
MockInstance::~MockInstance() = default;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
