#include "source/extensions/router/cluster_specifiers/header_weighted/config.h"
#include "source/extensions/router/cluster_specifiers/header_weighted/header_weighted.h"

#include "envoy/extensions/router/cluster_specifiers/header_weighted/v3/header_weighted.pb.validate.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {
namespace {

using testing::HasSubstr;
using testing::NiceMock;
using testing::Return;

class HeaderWeightedClusterSpecifierPluginTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    HeaderWeightedConfigProto proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    HeaderWeightedClusterSpecifierPluginFactoryConfig factory;

    plugin_ = factory.createClusterSpecifierPlugin(proto_config, server_factory_context_);
  }

  // cluster-a has weight 30 and cluster-b has weight 70, so the total weight
  // is 100 and a random value in [0, 30) selects cluster-a while a value in
  // [30, 100) selects cluster-b.
  const std::string normal_config_yaml = R"EOF(
header: x-target-cluster
clusters:
- name: cluster-a
  weight: 30
- name: cluster-b
  weight: 70
default_cluster: cluster-c
  )EOF";

  const std::string zero_weight_config_yaml = R"EOF(
header: x-target-cluster
clusters:
- name: cluster-a
  weight: 0
default_cluster: cluster-c
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Envoy::Router::ClusterSpecifierPlugin> plugin_;
};

TEST_F(HeaderWeightedClusterSpecifierPluginTest, HeaderSelectsCluster) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-target-cluster", "explicit-cluster"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("explicit-cluster", route->routeEntry()->clusterName());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, EmptyHeaderValueFallsBackToWeight) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-target-cluster", ""}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("cluster-a", route->routeEntry()->clusterName());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, MissingHeaderUsesWeight) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("cluster-a", route->routeEntry()->clusterName());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, MultiValueHeaderUsesFirstValue) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  headers.addCopy("x-target-cluster", "first-cluster");
  headers.addCopy("x-target-cluster", "second-cluster");
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("first-cluster", route->routeEntry()->clusterName());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, WeightedBoundaryValues) {
  setUpTest(normal_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};

  // The lower boundary of the first interval maps to cluster-a.
  {
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("cluster-a", route->routeEntry()->clusterName());
  }
  // The last value of the first interval still maps to cluster-a.
  {
    auto route = plugin_->route(mock_route, headers, stream_info_, 29);
    EXPECT_EQ("cluster-a", route->routeEntry()->clusterName());
  }
  // The first value of the second interval maps to cluster-b.
  {
    auto route = plugin_->route(mock_route, headers, stream_info_, 30);
    EXPECT_EQ("cluster-b", route->routeEntry()->clusterName());
  }
  // The last value of the second interval still maps to cluster-b.
  {
    auto route = plugin_->route(mock_route, headers, stream_info_, 99);
    EXPECT_EQ("cluster-b", route->routeEntry()->clusterName());
  }
  // A value equal to the total weight wraps around to the first interval.
  {
    auto route = plugin_->route(mock_route, headers, stream_info_, 100);
    EXPECT_EQ("cluster-a", route->routeEntry()->clusterName());
  }
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, ZeroTotalWeightFallsBackToDefault) {
  setUpTest(zero_weight_config_yaml);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 42);
  EXPECT_EQ("cluster-c", route->routeEntry()->clusterName());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, ValidateClustersAllPresent) {
  setUpTest(normal_config_yaml);

  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, hasCluster("cluster-a")).WillRepeatedly(Return(true));
  EXPECT_CALL(cm, hasCluster("cluster-b")).WillRepeatedly(Return(true));
  EXPECT_CALL(cm, hasCluster("cluster-c")).WillRepeatedly(Return(true));

  EXPECT_TRUE(plugin_->validateClusters(cm).ok());
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, ValidateClustersUnknownCluster) {
  setUpTest(normal_config_yaml);

  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, hasCluster("cluster-a")).WillRepeatedly(Return(false));
  EXPECT_CALL(cm, hasCluster("cluster-b")).WillRepeatedly(Return(true));
  EXPECT_CALL(cm, hasCluster("cluster-c")).WillRepeatedly(Return(true));

  const absl::Status status = plugin_->validateClusters(cm);
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
  EXPECT_THAT(status.message(), HasSubstr("cluster-a"));
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, ValidateClustersUnknownDefaultCluster) {
  setUpTest(normal_config_yaml);

  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, hasCluster("cluster-a")).WillRepeatedly(Return(true));
  EXPECT_CALL(cm, hasCluster("cluster-b")).WillRepeatedly(Return(true));
  EXPECT_CALL(cm, hasCluster("cluster-c")).WillRepeatedly(Return(false));

  const absl::Status status = plugin_->validateClusters(cm);
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());
  EXPECT_THAT(status.message(), HasSubstr("cluster-c"));
  EXPECT_THAT(status.message(), HasSubstr("default"));
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, RejectEmptyHeaderName) {
  const std::string yaml = R"EOF(
header: ""
clusters:
- name: cluster-a
  weight: 30
default_cluster: cluster-c
  )EOF";
  HeaderWeightedConfigProto proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, RejectEmptyClusterList) {
  const std::string yaml = R"EOF(
header: x-target-cluster
clusters: []
default_cluster: cluster-c
  )EOF";
  HeaderWeightedConfigProto proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, RejectEmptyDefaultCluster) {
  const std::string yaml = R"EOF(
header: x-target-cluster
clusters:
- name: cluster-a
  weight: 30
default_cluster: ""
  )EOF";
  HeaderWeightedConfigProto proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, RejectEmptyClusterName) {
  const std::string yaml = R"EOF(
header: x-target-cluster
clusters:
- name: ""
  weight: 30
default_cluster: cluster-c
  )EOF";
  HeaderWeightedConfigProto proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

TEST_F(HeaderWeightedClusterSpecifierPluginTest, RejectInvalidHeaderName) {
  // With well_known_regex: HTTP_HEADER_NAME and strict: false, the validation
  // only rejects strings containing \0, \n, or \r. A header name with an
  // embedded newline must therefore be rejected.
  const std::string yaml = R"EOF(
header: "x\ny"
clusters:
- name: cluster-a
  weight: 30
default_cluster: cluster-c
  )EOF";
  HeaderWeightedConfigProto proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

} // namespace
} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
