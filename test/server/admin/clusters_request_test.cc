#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/clusters_params.h"
#include "source/server/admin/clusters_request.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using testing::Return;
using testing::ReturnRef;

class BaseClustersRequestFixture : public testing::Test {
public:
  BaseClustersRequestFixture() : 
    cluster_info_name_("") {}
protected:
  using ClustersRequestPtr = std::unique_ptr<ClustersRequest>;

  ClustersRequestPtr makeRequest(uint64_t chunk_limit, Instance& server, ClustersParams& params) {
    return std::make_unique<ClustersRequest>(chunk_limit, server, params);
  }

  struct ResponseResult {
    Http::Code code_;
    Buffer::OwnedImpl data_;
  };

  ResponseResult response(ClustersRequest& request, bool drain_after_next_chunk) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl result_data;
    while (request.nextChunk(buffer)) {
      if (drain_after_next_chunk) {
        result_data.add(buffer);
        buffer.drain(buffer.length());
      }
    }
    return {
        /* code=*/code,
        /* data=*/drain_after_next_chunk ? std::move(result_data) : std::move(buffer),
    };
  }

const std::string& getClusterInfoName() {
  return cluster_info_name_;
}

private:
  const std::string cluster_info_name_;
};

struct VerifyJsonOutputParameters {
  bool drain_;
};

class VerifyJsonOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyJsonOutputParameters> {};

TEST_P(VerifyJsonOutputFixture, VerifyJsonOutput) {
  VerifyJsonOutputParameters params = GetParam();
  MockInstance mock_server;
  Upstream::MockCluster mock_cluster;
  Upstream::MockClusterManager mock_cluster_manager;
  Buffer::OwnedImpl buffer;
  ClustersParams clusters_params;
  Upstream::ClusterManager::ClusterInfoMaps cluster_info_maps;
  clusters_params.format_ = ClustersParams::Format::Json;
  EXPECT_CALL(mock_server, clusterManager()).WillOnce(ReturnRef(mock_cluster_manager));
  EXPECT_CALL(mock_cluster_manager, clusters()).WillOnce(Return(cluster_info_maps));

  ResponseResult result = response(*makeRequest(1, mock_server, clusters_params), params.drain_);

  EXPECT_EQ(result.code_, Http::Code::OK);
  // TODO(demtiriswan) add expection for JSON here based on mock function results
  // EXPECT_EQ(result.data_.toString(), "{}");
}

// constexpr VerifyJsonOutputParameters VERIFY_JSON_CASES[] = {
//     {/* drain_=*/true},
//     {/* drain_=*/false},
// };

// INSTANTIATE_TEST_SUITE_P(VerifyJsonOutput, VerifyJsonOutputFixture,
//                          testing::ValuesIn<VerifyJsonOutputParameters>(VERIFY_JSON_CASES));

// struct VerifyTextOutputParameters {
//   bool drain_;
// };

// class VerifyTextOutputFixture : public BaseClustersRequestFixture,
//                                 public testing::WithParamInterface<VerifyTextOutputParameters> {};

// // TODO(demitriswan) Implement test for text output verification.
// TEST_P(VerifyTextOutputFixture, VerifyTextOutput) {}

// constexpr VerifyTextOutputParameters VERIFY_TEXT_CASES[] = {
//     {/* drain_=*/true},
//     {/* drain_=*/false},
// };

// INSTANTIATE_TEST_SUITE_P(VerifyTextOutput, VerifyTextOutputFixture,
//                          testing::ValuesIn<VerifyTextOutputParameters>(VERIFY_TEXT_CASES));

class Foo {
public:
  Foo(std::unique_ptr<Json::Streamer> streamer, Buffer::Instance& buffer) : streamer_(std::move(streamer)), buffer_(buffer) {
    array_ = streamer_->makeRootArray();
  }
  void foo(Buffer::Instance& buffer) {
    array_->addNumber(int64_t(1));
    buffer.move(buffer_); 
  }
  std::unique_ptr<Json::Streamer> streamer_;
  Buffer::Instance& buffer_;
  Json::Streamer::ArrayPtr array_;
};

TEST(Json, Verify) {
  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl buffer;
  {
    Foo foo(std::make_unique<Json::Streamer>(buffer), buffer);
    foo.foo(request_buffer);
    foo.foo(request_buffer);
  }
  request_buffer.move(buffer);
  EXPECT_EQ(request_buffer.toString(), "[1,1]");
}

} // namespace Server
} // namespace Envoy
