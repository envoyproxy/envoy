#include "test/mocks/http/conn_pool.h"

#include "test/mocks/upstream/host.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

MockCancellable::MockCancellable() = default;
MockCancellable::~MockCancellable() = default;

MockInstance::MockInstance() : host_{new testing::NiceMock<Upstream::MockHostDescription>()} {}
MockInstance::~MockInstance() = default;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
