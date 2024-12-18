#include "test/extensions/filters/http/ext_proc/mock_server.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ::testing::_;
using ::testing::Invoke;

MockClient::MockClient() {
  EXPECT_CALL(*this, sendRequest(_, _, _, _, _))
      .WillRepeatedly(
          Invoke([](envoy::service::ext_proc::v3::ProcessingRequest&& request, bool end_stream,
                    const uint64_t, RequestCallbacks*, StreamBase* stream) {
            if (stream != nullptr) {
              ExternalProcessorStream* grpc_stream = dynamic_cast<ExternalProcessorStream*>(stream);
              grpc_stream->send(std::move(request), end_stream);
            }
          }));
}
MockClient::~MockClient() = default;

MockStream::MockStream() {
  ON_CALL(*this, closeLocalStream()).WillByDefault(Invoke([this]() {
    EXPECT_FALSE(local_closed_);
    local_closed_ = true;
    return local_closed_;
  }));
  ON_CALL(*this, remoteClosed()).WillByDefault(Invoke([this]() { return remote_closed_; }));
  ON_CALL(*this, localClosed()).WillByDefault(Invoke([this]() { return local_closed_; }));
}
MockStream::~MockStream() = default;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
