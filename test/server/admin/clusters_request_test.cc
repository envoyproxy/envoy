#include <memory>

#include "envoy/http/codes.h"
#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_request.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class BaseClustersRequestFixture : public testing::Test {
protected:
  using ClustersRequestPtr = std::unique_ptr<ClustersRequest>;

  ClustersRequestPtr makeRequest(Instance& server) {
    return std::make_unique<ClustersRequest>(server);
  }

  struct ResponseResult {
    Http::Code code_;
    Buffer::OwnedImpl data_;
  };

  ResponseResult response(ClustersRequest& request, bool drain_after_next_chunk) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    Buffer::OwnedImpl data;
    Buffer::OwnedImpl result_data;
    while (request.nextChunk(data)) {
      if (drain_after_next_chunk) {
        result_data.add(data);
        data.drain(data.length());
      }
    }
    return {
        /* code=*/code,
        /* data=*/drain_after_next_chunk ? std::move(result_data) : std::move(data),
    };
  }
};

struct VerifyJsonOutputParameters {
  bool drain_;
};

class VerifyJsonOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyJsonOutputParameters> {};

TEST_P(VerifyJsonOutputFixture, VerifyJsonOutput) {
  VerifyJsonOutputParameters params = GetParam();
  MockInstance mock_server;
  ResponseResult result = response(*makeRequest(mock_server), params.drain_);
  EXPECT_EQ(result.code_, Http::Code::OK);
  // TODO(demtiriswan) add expection for JSON here based on mock function results
  // EXPECT_EQ(result.data_.toString(), "{}");
}

constexpr VerifyJsonOutputParameters VERIFY_JSON_CASES[] = {
    {/* drain_=*/true},
    {/* drain_=*/false},
};

INSTANTIATE_TEST_SUITE_P(VerifyJsonOutput, VerifyJsonOutputFixture,
                         testing::ValuesIn<VerifyJsonOutputParameters>(VERIFY_JSON_CASES));

} // namespace Server
} // namespace Envoy
