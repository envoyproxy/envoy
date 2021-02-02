#include "common/local_reply/local_reply.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace LocalReply {
class MockLocalReply : public LocalReply {
public:
  MockLocalReply();
  ~MockLocalReply() override;

  MOCK_METHOD(void, rewrite,
              (const Http::RequestHeaderMap* request_headers,
               Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
               Http::Code& code, std::string& body, absl::string_view& content_type),
              (const));

  MOCK_METHOD(bool, match,
              (const Http::RequestHeaderMap* request_headers,
               const Http::ResponseHeaderMap& response_headers,
               const Http::ResponseTrailerMap* response_trailers,
               StreamInfo::StreamInfo& stream_info),
              (const));
};
} // namespace LocalReply
} // namespace Envoy
