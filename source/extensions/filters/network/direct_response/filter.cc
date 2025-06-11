#include "source/extensions/filters/network/direct_response/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

Network::FilterStatus DirectResponseFilter::onNewConnection() {
  auto& connection = read_callbacks_->connection();
  ENVOY_CONN_LOG(trace, "direct_response: new connection", connection);
  bool close_after_response = !keep_open_after_response_;
  if (!response_.empty()) {
    Buffer::OwnedImpl data(response_);
    connection.write(data, close_after_response);
    ASSERT(0 == data.length());
  }
  connection.streamInfo().setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().DirectResponse);
  if (close_after_response) {
    connection.close(Network::ConnectionCloseType::FlushWrite);
  }
  return Network::FilterStatus::StopIteration;
}

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
