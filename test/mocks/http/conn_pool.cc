#include "test/mocks/http/conn_pool.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

MockCancellable::MockCancellable() = default;
MockCancellable::~MockCancellable() = default;

MockInstance::MockInstance()
    : host_{std::make_shared<testing::NiceMock<Upstream::MockHostDescription>>()} {}
MockInstance::~MockInstance() = default;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
