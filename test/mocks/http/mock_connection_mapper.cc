#include "test/mocks/http/mock_connection_mapper.h"

#include "envoy/http/conn_pool.h"

namespace Envoy {
namespace Http {

MockConnectionMapper::MockConnectionMapper()
    : builder_{[] { return Http::ConnectionPool::InstancePtr(); }} {}

MockConnectionMapper::~MockConnectionMapper() = default;
} // namespace Http
} // namespace Envoy
