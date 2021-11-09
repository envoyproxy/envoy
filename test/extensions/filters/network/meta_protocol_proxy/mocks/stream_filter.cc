#include "test/extensions/filters/network/meta_protocol_proxy/mocks/stream_filter.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, onStreamEncoded(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, onStreamDecoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
