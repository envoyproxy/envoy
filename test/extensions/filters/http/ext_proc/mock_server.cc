#include "test/extensions/filters/http/ext_proc/mock_server.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

MockClient::MockClient() {
  EXPECT_CALL(*this, stream()).WillRepeatedly(testing::Invoke([this]() { return stream_; }));

  EXPECT_CALL(*this, setStream(testing::_))
      .WillRepeatedly(
          testing::Invoke([this](ExternalProcessorStream* stream) -> void { stream_ = stream; }));
}
MockClient::~MockClient() = default;

MockStream::MockStream() = default;
MockStream::~MockStream() = default;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
