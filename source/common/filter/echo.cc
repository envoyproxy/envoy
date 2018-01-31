#include "common/filter/echo.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Filter {

Network::FilterStatus Echo::onData(Buffer::Instance& data, bool last_byte) {
  ENVOY_CONN_LOG(trace, "echo: got {} bytes", read_callbacks_->connection(), data.length());
  read_callbacks_->connection().write(data, last_byte);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

} // namespace Filter
} // namespace Envoy
