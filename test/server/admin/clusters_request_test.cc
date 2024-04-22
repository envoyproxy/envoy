#include <cstdint>
#include <functional>
#include <memory>
#include <iostream>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/server/instance.h"
#include "envoy/stream_info/stream_info.h"

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

using testing::NiceMock;
using testing::ReturnPointee;
using testing::ReturnRef;

class BaseClustersRequestFixture : public testing::Test {
protected:
  BaseClustersRequestFixture() {
    ON_CALL(mock_server_, clusterManager()).WillByDefault(ReturnRef(mock_cluster_manager_));
    ON_CALL(mock_cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_info_maps_));
  }

  using ClustersRequestPtr = std::unique_ptr<ClustersRequest>;

  ClustersRequestPtr makeRequest(uint64_t chunk_limit, ClustersParams& params) {
    return std::make_unique<ClustersRequest>(chunk_limit, mock_server_, params);
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
        result_data.move(buffer);
      }
    }
    if (drain_after_next_chunk) {
      result_data.move(buffer);
    }
    return {
        /* code=*/code,
        /* data=*/drain_after_next_chunk ? std::move(result_data) : std::move(buffer),
    };
  }

  void loadNewMockClusterByName(NiceMock<Upstream::MockClusterMockPrioritySet>& mock_cluster, std::string_view name) {
    mock_cluster.info_->name_ = name;
    cluster_info_maps_.active_clusters_.emplace(name, std::ref(mock_cluster));
  }

  NiceMock<MockInstance> mock_server_;
  NiceMock<Upstream::MockClusterManager> mock_cluster_manager_;
  Upstream::ClusterManager::ClusterInfoMaps cluster_info_maps_;
};

struct VerifyJsonOutputParameters {
  bool drain_;
};

class VerifyJsonOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyJsonOutputParameters> {};

TEST_P(VerifyJsonOutputFixture, VerifyJsonOutput) {
  constexpr int chunk_limit = 1;  // Small chunk limit will force next chunk to be called for each Cluster.
  VerifyJsonOutputParameters params = GetParam();
  Buffer::OwnedImpl buffer;
  ClustersParams clusters_params; 
  clusters_params.format_ = ClustersParams::Format::Json;

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster;
  loadNewMockClusterByName(test_cluster, "test_cluster");

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster2;
  loadNewMockClusterByName(test_cluster2, "test_cluster2");
  

  ResponseResult result = response(*makeRequest(chunk_limit, clusters_params), params.drain_);

  EXPECT_EQ(result.code_, Http::Code::OK);
  EXPECT_EQ(result.data_.toString(), R"EOF({"cluster_statuses":[{"name":"test_cluster","observability_name":"observability_name"},{"name":"test_cluster2","observability_name":"observability_name"}]})EOF");
}

constexpr VerifyJsonOutputParameters VERIFY_JSON_CASES[] = {
    {/* drain_=*/false},
    {/* drain_=*/true},
};

INSTANTIATE_TEST_SUITE_P(VerifyJsonOutput, VerifyJsonOutputFixture,
                         testing::ValuesIn<VerifyJsonOutputParameters>(VERIFY_JSON_CASES));

struct VerifyTextOutputParameters {
  bool drain_;
};

class VerifyTextOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyTextOutputParameters> {};

// TODO(demitriswan) Implement test for text output verification.
TEST_P(VerifyTextOutputFixture, VerifyTextOutput) {}

constexpr VerifyTextOutputParameters VERIFY_TEXT_CASES[] = {
    {/* drain_=*/true},
    {/* drain_=*/false},
};

INSTANTIATE_TEST_SUITE_P(VerifyTextOutput, VerifyTextOutputFixture,
                         testing::ValuesIn<VerifyTextOutputParameters>(VERIFY_TEXT_CASES));


TEST(Json, VerifyArrayPtrDestructionTerminatesJsonArray) {
  class Foo {
  public:
    Foo(std::unique_ptr<Json::Streamer> streamer, Buffer::Instance& buffer) : streamer_(std::move(streamer)), buffer_(buffer) {
      array_ = streamer_->makeRootArray();
    }
    void foo(Buffer::Instance& buffer, int64_t number) {
      array_->addNumber(number);
      buffer.move(buffer_);
    }
    std::unique_ptr<Json::Streamer> streamer_;
    Buffer::Instance& buffer_;
    Json::Streamer::ArrayPtr array_;
  };

  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl buffer;
  {
    Foo foo(std::make_unique<Json::Streamer>(buffer), buffer);
    foo.foo(request_buffer, 1);
    foo.foo(request_buffer, 2);
  }
  request_buffer.move(buffer);
  EXPECT_EQ(request_buffer.toString(), "[1,2]");
}

} // namespace Server
} // namespace Envoy
