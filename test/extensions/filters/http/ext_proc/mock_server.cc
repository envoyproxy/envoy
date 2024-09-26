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
            ExternalProcessorStream* grpc_stream = dynamic_cast<ExternalProcessorStream*>(stream);
            grpc_stream->send(std::move(request), end_stream);
          }));
}
MockClient::~MockClient() = default;

MockStream::MockStream() = default;
MockStream::~MockStream() = default;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
