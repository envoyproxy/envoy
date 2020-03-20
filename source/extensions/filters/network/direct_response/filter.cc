#include "extensions/filters/network/direct_response/filter.h"

#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

Network::FilterStatus DirectResponseFilter::onNewConnection() {
  ENVOY_CONN_LOG(trace, "direct_response: new connection", read_callbacks_->connection());
  if (response_.size() > 0) {
    Buffer::OwnedImpl data(response_);
    read_callbacks_->connection().write(data, false);
    ASSERT(0 == data.length());
  }
  read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  return Network::FilterStatus::StopIteration;
}

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
