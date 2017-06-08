#include "common/filter/echo.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Filter {

Network::FilterStatus Echo::onData(Buffer::Instance& data) {
  conn_log_facility(trace, "echo: got {} bytes", read_callbacks_->connection(), data.length());
  read_callbacks_->connection().write(data);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

} // Filter
} // Envoy
