#include "echo.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Filter {

Network::FilterStatus Echo::onData(Buffer::Instance& data) {
  conn_log_trace("echo: got {} bytes", read_callbacks_->connection(), data.length());
  read_callbacks_->connection().write(data);
  data.drain(data.length());
  return Network::FilterStatus::StopIteration;
}

} // Filter
