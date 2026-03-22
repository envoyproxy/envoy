#include "source/common/network/address_impl.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/http/peer_metadata/source/peer_metadata.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Istio::Common::WorkloadMetadataObject;
using testing::HasSubstr;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeerMetadata {
namespace {

class MockSingletonManager : public Singleton::Manager {
public:
  MockSingletonManager() = default;
  MOCK_METHOD(Singleton::InstanceSharedPtr, get,
              (const std::string& name, Singleton::SingletonFactoryCb cb, bool pin));
};

class MockWorkloadMetadataProvider
    : public Extensions::Common::WorkloadDiscovery::WorkloadMetadataProvider,
      public Singleton::Instance {
public:
  MockWorkloadMetadataProvider() = default;
  MOCK_METHOD(absl::optional<WorkloadMetadataObject>, getMetadata,
              (const Network::Address::InstanceConstSharedPtr& address));
};

class PeerMetadataTest : public testing::Test {
protected:
  PeerMetadataTest() {
    ON_CALL(context_.server_factory_context_, singletonManager())
        .WillByDefault(ReturnRef(singleton_manager_));
    metadata_provider_ = std::make_shared<NiceMock<MockWorkloadMetadataProvider>>();
    ON_CALL(singleton_manager_, get(HasSubstr("workload_metadata_provider"), _, _))
        .WillByDefault(Return(metadata_provider_));
  }
  void initialize(const std::string& yaml_config) {
    TestUtility::loadFromYaml(yaml_config, config_);
    FilterConfigFactory factory;
    Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config_, "", context_).value();
    Http::MockFilterChainFactoryCallbacks filter_callback;
    ON_CALL(filter_callback, addStreamFilter(_)).WillByDefault(testing::SaveArg<0>(&filter_));
    EXPECT_CALL(filter_callback, addStreamFilter(_));
    cb(filter_callback);
    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  }
  void checkNoPeer(bool downstream) {
    EXPECT_FALSE(stream_info_.filterState()->hasDataWithName(
        downstream ? Istio::Common::DownstreamPeer : Istio::Common::UpstreamPeer));
  }
  void checkPeerNamespace(bool downstream, const std::string& expected) {
    const auto* cel_state =
        stream_info_.filterState()
            ->getDataReadOnly<Envoy::Extensions::Filters::Common::Expr::CelState>(
                downstream ? Istio::Common::DownstreamPeer : Istio::Common::UpstreamPeer);
    Protobuf::Struct obj;
    ASSERT_TRUE(obj.ParseFromString(cel_state->value()));
    EXPECT_EQ(expected, extractString(obj, "namespace"));
  }

  absl::string_view extractString(const Protobuf::Struct& metadata, absl::string_view key) {
    const auto& it = metadata.fields().find(key);
    if (it == metadata.fields().end()) {
      return {};
    }
    return it->second.string_value();
  }

  void checkShared(bool expected) {
    EXPECT_EQ(expected,
              stream_info_.filterState()->objectsSharedWithUpstreamConnection()->size() > 0);
  }
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockSingletonManager> singleton_manager_;
  std::shared_ptr<NiceMock<MockWorkloadMetadataProvider>> metadata_provider_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  io::istio::http::peer_metadata::Config config_;
  Http::StreamFilterSharedPtr filter_;
};

TEST_F(PeerMetadataTest, None) {
  initialize("{}");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, DownstreamXDSNone) {
  EXPECT_CALL(*metadata_provider_, getMetadata(_)).WillRepeatedly(Return(std::nullopt));
  initialize(R"EOF(
    downstream_discovery:
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, DownstreamXDS) {
  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "127.0.0.1")) {
          return {pod};
        }
        return {};
      }));
  initialize(R"EOF(
    downstream_discovery:
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkPeerNamespace(true, "default");
  checkNoPeer(false);
  checkShared(false);
}

TEST_F(PeerMetadataTest, UpstreamXDS) {
  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "foo", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "10.0.0.1")) {
          return {pod};
        }
        return {};
      }));
  initialize(R"EOF(
    upstream_discovery:
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "foo");
}

TEST_F(PeerMetadataTest, UpstreamXDSInternal) {
  Network::Address::InstanceConstSharedPtr upstream_address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("internal_address", "endpoint_id");
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> upstream_host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  EXPECT_CALL(*upstream_host, address()).WillRepeatedly(Return(upstream_address));
  stream_info_.upstreamInfo()->setUpstreamHost(upstream_host);
  auto host_metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ON_CALL(*upstream_host, metadata()).WillByDefault(testing::Return(host_metadata));
  TestUtility::loadFromYaml(R"EOF(
  filter_metadata:
    envoy.filters.listener.original_dst:
      local: 127.0.0.100:80
  )EOF",
                            *host_metadata);

  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "foo", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "127.0.0.100")) {
          return {pod};
        }
        return {};
      }));
  initialize(R"EOF(
    upstream_discovery:
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "foo");
}

TEST_F(PeerMetadataTest, DownstreamMXEmpty) {
  initialize(R"EOF(
    downstream_discovery:
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

constexpr absl::string_view SampleIstioHeader =
    "ChIKBWlzdGlvEgkaB3NpZGVjYXIKDgoIU1RTX1BPUlQSAhoAChEKB01FU0hfSUQSBhoEbWVzaAocChZTVEFDS0RSSVZFUl"
    "9UT0tFTl9GSUxFEgIaAAowCihTVEFDS0RSSVZFUl9MT0dHSU5HX0VYUE9SVF9JTlRFUlZBTF9TRUNTEgQaAjIwCjYKDElO"
    "U1RBTkNFX0lQUxImGiQxMC41Mi4wLjM0LGZlODA6OmEwNzU6MTFmZjpmZTVlOmYxY2QKFAoDYXBwEg0aC3Byb2R1Y3RwYW"
    "dlCisKG1NFQ1VSRV9TVEFDS0RSSVZFUl9FTkRQT0lOVBIMGgpsb2NhbGhvc3Q6Cl0KGmt1YmVybmV0ZXMuaW8vbGltaXQt"
    "cmFuZ2VyEj8aPUxpbWl0UmFuZ2VyIHBsdWdpbiBzZXQ6IGNwdSByZXF1ZXN0IGZvciBjb250YWluZXIgcHJvZHVjdHBhZ2"
    "UKIQoNV09SS0xPQURfTkFNRRIQGg5wcm9kdWN0cGFnZS12MQofChFJTlRFUkNFUFRJT05fTU9ERRIKGghSRURJUkVDVAoe"
    "CgpDTFVTVEVSX0lEEhAaDmNsaWVudC1jbHVzdGVyCkkKD0lTVElPX1BST1hZX1NIQRI2GjRpc3Rpby1wcm94eTo0N2U0NT"
    "U5YjhlNGYwZDUxNmMwZDE3YjIzM2QxMjdhM2RlYjNkN2NlClIKBU9XTkVSEkkaR2t1YmVybmV0ZXM6Ly9hcGlzL2FwcHMv"
    "djEvbmFtZXNwYWNlcy9kZWZhdWx0L2RlcGxveW1lbnRzL3Byb2R1Y3RwYWdlLXYxCsEBCgZMQUJFTFMStgEqswEKFAoDYX"
    "BwEg0aC3Byb2R1Y3RwYWdlCiEKEXBvZC10ZW1wbGF0ZS1oYXNoEgwaCjg0OTc1YmM3NzgKMwofc2VydmljZS5pc3Rpby5p"
    "by9jYW5vbmljYWwtbmFtZRIQGg5wcm9kdWN0cGFnZS12MQoyCiNzZXJ2aWNlLmlzdGlvLmlvL2Nhbm9uaWNhbC1yZXZpc2"
    "lvbhILGgl2ZXJzaW9uLTEKDwoHdmVyc2lvbhIEGgJ2MQopCgROQU1FEiEaH3Byb2R1Y3RwYWdlLXYxLTg0OTc1YmM3Nzgt"
    "cHh6MncKLQoIUE9EX05BTUUSIRofcHJvZHVjdHBhZ2UtdjEtODQ5NzViYzc3OC1weHoydwoaCg1JU1RJT19WRVJTSU9OEg"
    "kaBzEuNS1kZXYKHwoVSU5DTFVERV9JTkJPVU5EX1BPUlRTEgYaBDkwODAKmwEKEVBMQVRGT1JNX01FVEFEQVRBEoUBKoIB"
    "CiYKFGdjcF9na2VfY2x1c3Rlcl9uYW1lEg4aDHRlc3QtY2x1c3RlcgocCgxnY3BfbG9jYXRpb24SDBoKdXMtZWFzdDQtYg"
    "odCgtnY3BfcHJvamVjdBIOGgx0ZXN0LXByb2plY3QKGwoSZ2NwX3Byb2plY3RfbnVtYmVyEgUaAzEyMwopCg9TRVJWSUNF"
    "X0FDQ09VTlQSFhoUYm9va2luZm8tcHJvZHVjdHBhZ2UKHQoQQ09ORklHX05BTUVTUEFDRRIJGgdkZWZhdWx0Cg8KB3Zlcn"
    "Npb24SBBoCdjEKHgoYU1RBQ0tEUklWRVJfUk9PVF9DQV9GSUxFEgIaAAohChFwb2QtdGVtcGxhdGUtaGFzaBIMGgo4NDk3"
    "NWJjNzc4Ch8KDkFQUF9DT05UQUlORVJTEg0aC3Rlc3QsYm9uemFpChYKCU5BTUVTUEFDRRIJGgdkZWZhdWx0CjMKK1NUQU"
    "NLRFJJVkVSX01PTklUT1JJTkdfRVhQT1JUX0lOVEVSVkFMX1NFQ1MSBBoCMjA";

TEST_F(PeerMetadataTest, DownstreamFallbackFirst) {
  request_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  request_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  EXPECT_CALL(*metadata_provider_, getMetadata(_)).Times(0);
  initialize(R"EOF(
    downstream_discovery:
      - istio_headers: {}
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkPeerNamespace(true, "default");
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, DownstreamFallbackSecond) {
  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "127.0.0.1")) { // remote address
          return {pod};
        }
        return {};
      }));
  initialize(R"EOF(
    downstream_discovery:
      - istio_headers: {}
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkPeerNamespace(true, "default");
  checkNoPeer(false);
}

TEST(MXMethod, Cache) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  absl::flat_hash_set<std::string> additional_labels;
  MXMethod method(true, additional_labels, context);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  const int32_t max = 1000;
  for (int32_t run = 0; run < 3; run++) {
    for (int32_t i = 0; i < max; i++) {
      std::string id = absl::StrCat("test-", i);
      request_headers.setReference(Headers::get().ExchangeMetadataHeaderId, id);
      request_headers.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
      Context ctx;
      const auto result = method.derivePeerInfo(stream_info, request_headers, ctx);
      EXPECT_TRUE(result.has_value());
    }
  }
}

TEST_F(PeerMetadataTest, DownstreamMX) {
  request_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  request_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  initialize(R"EOF(
    downstream_discovery:
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkPeerNamespace(true, "default");
  checkNoPeer(false);
  checkShared(false);
}

TEST_F(PeerMetadataTest, UpstreamMX) {
  response_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  response_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  initialize(R"EOF(
    upstream_discovery:
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "default");
}

TEST_F(PeerMetadataTest, UpstreamFallbackFirst) {
  EXPECT_CALL(*metadata_provider_, getMetadata(_)).Times(0);
  response_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  response_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  initialize(R"EOF(
    upstream_discovery:
      - istio_headers: {}
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "default");
}

TEST_F(PeerMetadataTest, UpstreamFallbackSecond) {
  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "foo", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "10.0.0.1")) { // upstream host address
          return {pod};
        }
        return {};
      }));
  initialize(R"EOF(
    upstream_discovery:
      - istio_headers: {}
      - workload_discovery: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "foo");
}

TEST_F(PeerMetadataTest, UpstreamFallbackFirstXDS) {
  const WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "foo", "foo", "foo-service",
                                   "v1alpha3", "", "", Istio::Common::WorkloadType::Pod, "");
  EXPECT_CALL(*metadata_provider_, getMetadata(_))
      .WillRepeatedly(Invoke([&](const Network::Address::InstanceConstSharedPtr& address)
                                 -> absl::optional<WorkloadMetadataObject> {
        if (absl::StartsWith(address->asStringView(), "10.0.0.1")) { // upstream host address
          return {pod};
        }
        return {};
      }));
  response_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  response_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  initialize(R"EOF(
    upstream_discovery:
      - workload_discovery: {}
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkPeerNamespace(false, "foo");
}
TEST_F(PeerMetadataTest, DownstreamMXPropagation) {
  initialize(R"EOF(
    downstream_propagation:
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, DownstreamMXPropagationWithAdditionalLabels) {
  initialize(R"EOF(
    downstream_propagation:
      - istio_headers: {}
    additional_labels:
      - foo
      - bar
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, DownstreamMXDiscoveryPropagation) {
  request_headers_.setReference(Headers::get().ExchangeMetadataHeaderId, "test-pod");
  request_headers_.setReference(Headers::get().ExchangeMetadataHeader, SampleIstioHeader);
  initialize(R"EOF(
    downstream_discovery:
      - istio_headers: {}
    downstream_propagation:
      - istio_headers: {}
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(2, response_headers_.size());
  checkPeerNamespace(true, "default");
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, UpstreamMXPropagation) {
  initialize(R"EOF(
    upstream_propagation:
      - istio_headers:
          skip_external_clusters: false
  )EOF");
  EXPECT_EQ(2, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, UpstreamMXPropagationSkipNoMatch) {
  initialize(R"EOF(
    upstream_propagation:
      - istio_headers:
          skip_external_clusters: true
  )EOF");
  EXPECT_EQ(2, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, UpstreamMXPropagationSkip) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  auto metadata = TestUtility::parseYaml<envoy::config::core::v3::Metadata>(R"EOF(
      filter_metadata:
        istio:
          external: true
    )EOF");
  ON_CALL(stream_info_, upstreamClusterInfo()).WillByDefault(testing::Return(cluster_info_));
  ON_CALL(*cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));
  initialize(R"EOF(
    upstream_propagation:
      - istio_headers:
          skip_external_clusters: true
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

TEST_F(PeerMetadataTest, UpstreamMXPropagationSkipPassthrough) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  cluster_info_->name_ = "PassthroughCluster";
  ON_CALL(stream_info_, upstreamClusterInfo()).WillByDefault(testing::Return(cluster_info_));
  initialize(R"EOF(
    upstream_propagation:
      - istio_headers:
          skip_external_clusters: true
  )EOF");
  EXPECT_EQ(0, request_headers_.size());
  EXPECT_EQ(0, response_headers_.size());
  checkNoPeer(true);
  checkNoPeer(false);
}

} // namespace
} // namespace PeerMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
