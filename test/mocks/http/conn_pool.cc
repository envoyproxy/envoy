#include "test/mocks/http/conn_pool.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Http {
namespace ConnectionPool {

MockInstance::MockInstance()
    : host_{std::make_shared<testing::NiceMock<Upstream::MockHostDescription>>()} {
  ON_CALL(*this, host()).WillByDefault(Return(host_));
  ON_CALL(*this, addIdleCallback(_)).WillByDefault(SaveArg<0>(&idle_cb_));
  ON_CALL(*this, socketOptions()).WillByDefault(ReturnRef(socket_options_));
}
MockInstance::~MockInstance() = default;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
