#include "source/extensions/filters/network/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

Network::FilterStatus NetworkExtProcFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "ext_proc: new connection", read_callbacks_->connection());
  // Minimal implementation - just continue
  return Network::FilterStatus::Continue;
}

Network::FilterStatus NetworkExtProcFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(debug, "ext_proc: got {} bytes of data", read_callbacks_->connection(),
                 data.length());
  // Minimal implementation - just continue
  return Network::FilterStatus::Continue;
}

Network::FilterStatus NetworkExtProcFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(debug, "ext_proc: writing {} bytes of data", write_callbacks_->connection(),
                 data.length());
  // Minimal implementation - just continue
  return Network::FilterStatus::Continue;
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
