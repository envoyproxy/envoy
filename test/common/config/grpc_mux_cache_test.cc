#include "source/common/config/grpc_mux_cache.h"

#include "test/mocks/config/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

using testing::NiceMock;

class GrpcMuxCacheTest : public testing::Test {
public:
  GrpcMuxCacheTest() = default;

  envoy::config::core::v3::ConfigSource config_;
  GrpcMuxCache cache_;
};

TEST_F(GrpcMuxCacheTest, NoCacheHit) {
  config_.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto mock_mux = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called = false;
  auto mux = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called = true;
    return mock_mux;
  });
  EXPECT_TRUE(creator_called);
  EXPECT_EQ(mux, mock_mux);
}

TEST_F(GrpcMuxCacheTest, CacheHit) {
  config_.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto mock_mux = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called1 = false;
  auto mux1 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called1 = true;
    return mock_mux;
  });
  EXPECT_TRUE(creator_called1);
  EXPECT_EQ(mux1, mock_mux);

  bool creator_called2 = false;
  auto mux2 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called2 = true;
    return std::make_shared<NiceMock<MockGrpcMux>>();
  });
  EXPECT_FALSE(creator_called2);
  EXPECT_EQ(mux2, mock_mux);
}

TEST_F(GrpcMuxCacheTest, CacheHitNullWeakPtr) {
  config_.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto mock_mux1 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called1 = false;
  auto mux1 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called1 = true;
    return mock_mux1;
  });
  EXPECT_TRUE(creator_called1);
  EXPECT_EQ(mux1, mock_mux1);

  // Reset mux1 so the weak_ptr in cache expires.
  mock_mux1.reset();
  mux1.reset();

  auto mock_mux2 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called2 = false;
  auto mux2 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called2 = true;
    return mock_mux2;
  });
  EXPECT_TRUE(creator_called2);
  EXPECT_EQ(mux2, mock_mux2);
}

TEST_F(GrpcMuxCacheTest, ClearCache) {
  config_.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto mock_mux = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called1 = false;
  auto mux1 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called1 = true;
    return mock_mux;
  });
  EXPECT_TRUE(creator_called1);
  EXPECT_EQ(mux1, mock_mux);

  cache_.clear();

  bool creator_called2 = false;
  auto mux2 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.Type", [&]() {
    creator_called2 = true;
    return mock_mux;
  });
  EXPECT_TRUE(creator_called2);
  EXPECT_EQ(mux2, mock_mux);
}

TEST_F(GrpcMuxCacheTest, DifferentTypeUrlNoCacheHit) {
  config_.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  auto mock_mux1 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called1 = false;
  auto mux1 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.TypeA", [&]() {
    creator_called1 = true;
    return mock_mux1;
  });
  EXPECT_TRUE(creator_called1);
  EXPECT_EQ(mux1, mock_mux1);

  auto mock_mux2 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called2 = false;
  auto mux2 = cache_.getOrCreateMux(config_, "type.googleapis.com/some.TypeB", [&]() {
    creator_called2 = true;
    return mock_mux2;
  });
  EXPECT_TRUE(creator_called2);
  EXPECT_EQ(mux2, mock_mux2);
}

TEST_F(GrpcMuxCacheTest, DifferentConfigSourceNoCacheHit) {
  envoy::config::core::v3::ConfigSource config1;
  config1.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  config1.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");

  auto mock_mux1 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called1 = false;
  auto mux1 = cache_.getOrCreateMux(config1, "type.googleapis.com/some.Type", [&]() {
    creator_called1 = true;
    return mock_mux1;
  });
  EXPECT_TRUE(creator_called1);
  EXPECT_EQ(mux1, mock_mux1);

  envoy::config::core::v3::ConfigSource config2;
  config2.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  config2.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_2");

  auto mock_mux2 = std::make_shared<NiceMock<MockGrpcMux>>();
  bool creator_called2 = false;
  auto mux2 = cache_.getOrCreateMux(config2, "type.googleapis.com/some.Type", [&]() {
    creator_called2 = true;
    return mock_mux2;
  });
  EXPECT_TRUE(creator_called2);
  EXPECT_EQ(mux2, mock_mux2);
}

} // namespace
} // namespace Config
} // namespace Envoy
