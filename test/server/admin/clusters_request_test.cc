#include <memory>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_request.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class ClustersRequestTest : public testing::Test {
protected:
  std::unique_ptr<ClustersRequest> makeRequest() { return std::make_unique<ClustersRequest>(); }

  struct ResponseResult {
    Http::Code code_;
    Buffer::OwnedImpl data_;
  };

  // Executes a request, returning the rendered buffer as a string.
  ResponseResult response(ClustersRequest& request) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    Buffer::OwnedImpl data;
    while (request.nextChunk(data)) {
    }
    return {
        .code_ = code,
        .data_ = std::move(data),
    };
  }
};

} // namespace Server
} // namespace Envoy
