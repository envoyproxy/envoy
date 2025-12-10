#include "envoy/common/hashable.h"

#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/downstream_network_namespace.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class TransportSocketOptionsImplTest : public testing::Test {
public:
  TransportSocketOptionsImplTest()
      : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {}
  void setFilterStateObject(const std::string& key, const std::string& value) {
    auto* factory =
        Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(key);
    ASSERT_NE(nullptr, factory);
    EXPECT_EQ(key, factory->name());
    auto object = factory->createFromBytes(value);
    ASSERT_NE(nullptr, object);
    filter_state_.setData(key, std::move(object), StreamInfo::FilterState::StateType::ReadOnly,
                          StreamInfo::FilterState::LifeSpan::FilterChain);
  }

protected:
  StreamInfo::FilterStateImpl filter_state_;
};

TEST_F(TransportSocketOptionsImplTest, Nullptr) {
  EXPECT_EQ(nullptr, TransportSocketOptionsUtility::fromFilterState(filter_state_));
  filter_state_.setData(
      "random_key_has_no_effect", std::make_unique<UpstreamServerName>("www.example.com"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(nullptr, TransportSocketOptionsUtility::fromFilterState(filter_state_));
}

TEST_F(TransportSocketOptionsImplTest, SharedFilterState) {
  filter_state_.setData(
      "random_key_has_effect", std::make_unique<UpstreamServerName>("www.example.com"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain,
      StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  auto objects = transport_socket_options->downstreamSharedFilterStateObjects();
  EXPECT_EQ(1, objects.size());
  EXPECT_EQ("random_key_has_effect", objects.at(0).name_);
}

TEST_F(TransportSocketOptionsImplTest, UpstreamServer) {
  filter_state_.setData(
      UpstreamServerName::key(), std::make_unique<UpstreamServerName>("www.example.com"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state_.setData(ProxyProtocolFilterState::key(),
                        std::make_unique<ProxyProtocolFilterState>(Network::ProxyProtocolData{
                            Network::Address::InstanceConstSharedPtr(
                                new Network::Address::Ipv4Instance("202.168.0.13", 52000)),
                            Network::Address::InstanceConstSharedPtr(
                                new Network::Address::Ipv4Instance("174.2.2.222", 80))}),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::make_optional<std::string>("www.example.com"),
            transport_socket_options->serverNameOverride());
  EXPECT_EQ("202.168.0.13:52000",
            transport_socket_options->proxyProtocolOptions()->src_addr_->asStringView());
  EXPECT_TRUE(transport_socket_options->applicationProtocolListOverride().empty());
}

TEST_F(TransportSocketOptionsImplTest, ApplicationProtocols) {
  std::vector<std::string> http_alpns{Http::Utility::AlpnNames::get().Http2,
                                      Http::Utility::AlpnNames::get().Http11};
  filter_state_.setData(
      ApplicationProtocols::key(), std::make_unique<ApplicationProtocols>(http_alpns),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::nullopt, transport_socket_options->serverNameOverride());
  EXPECT_EQ(http_alpns, transport_socket_options->applicationProtocolListOverride());
}

TEST_F(TransportSocketOptionsImplTest, Both) {
  std::vector<std::string> http_alpns{Http::Utility::AlpnNames::get().Http2,
                                      Http::Utility::AlpnNames::get().Http11};
  filter_state_.setData(
      UpstreamServerName::key(), std::make_unique<UpstreamServerName>("www.example.com"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state_.setData(
      ApplicationProtocols::key(), std::make_unique<ApplicationProtocols>(http_alpns),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::make_optional<std::string>("www.example.com"),
            transport_socket_options->serverNameOverride());
  EXPECT_EQ(http_alpns, transport_socket_options->applicationProtocolListOverride());
}

class TestTransportSocketFactory : public CommonUpstreamTransportSocketFactory {
public:
  TransportSocketPtr
  createTransportSocket(TransportSocketOptionsConstSharedPtr,
                        std::shared_ptr<const Upstream::HostDescription>) const override {
    return nullptr;
  }
  absl::string_view defaultServerNameIndication() const override { return ""; }
  bool implementsSecureTransport() const override { return false; }
};

class NonHashableObj : public StreamInfo::FilterState::Object {};
class HashableObj : public StreamInfo::FilterState::Object, public Hashable {
  absl::optional<uint64_t> hash() const override { return 12345; };
};

TEST_F(TransportSocketOptionsImplTest, FilterStateHashable) {
  filter_state_.setData("hashable", std::make_shared<HashableObj>(),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  TestTransportSocketFactory factory;
  std::vector<uint8_t> keys;
  factory.hashKey(keys, transport_socket_options);
  EXPECT_GT(keys.size(), 0);
}

TEST_F(TransportSocketOptionsImplTest, FilterStateNonHashable) {
  filter_state_.setData("non-hashable", std::make_shared<NonHashableObj>(),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  TestTransportSocketFactory factory;
  std::vector<uint8_t> keys;
  factory.hashKey(keys, transport_socket_options);
  EXPECT_EQ(keys.size(), 0);
}

TEST_F(TransportSocketOptionsImplTest, DynamicObjects) {
  setFilterStateObject(UpstreamServerName::key(), "www.example.com");
  setFilterStateObject(ApplicationProtocols::key(), "h2,http/1.1");
  setFilterStateObject(UpstreamSubjectAltNames::key(), "www.example.com,example.com");
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::make_optional<std::string>("www.example.com"),
            transport_socket_options->serverNameOverride());
  std::vector<std::string> http_alpns{"h2", "http/1.1"};
  EXPECT_EQ(http_alpns, transport_socket_options->applicationProtocolListOverride());
  std::vector<std::string> sans{"www.example.com", "example.com"};
  EXPECT_EQ(sans, transport_socket_options->verifySubjectAltNameListOverride());
}

TEST_F(TransportSocketOptionsImplTest, DownstreamNetworkNamespace) {
  const std::string network_namespace_filepath = "/var/run/netns/production";

  // Create the object directly.
  auto network_namespace_obj =
      std::make_unique<DownstreamNetworkNamespace>(network_namespace_filepath);
  EXPECT_EQ(network_namespace_filepath, network_namespace_obj->value());
  EXPECT_EQ(absl::make_optional<std::string>(network_namespace_filepath),
            network_namespace_obj->serializeAsString());

  // Test key.
  EXPECT_EQ("envoy.network.network_namespace", DownstreamNetworkNamespace::key());
}

TEST_F(TransportSocketOptionsImplTest, NetworkNamespaceSharedWithUpstream) {
  const std::string network_namespace_filepath = "/var/run/netns/staging";

  // Set network namespace as shared with upstream connection.
  filter_state_.setData(DownstreamNetworkNamespace::key(),
                        std::make_unique<DownstreamNetworkNamespace>(network_namespace_filepath),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection,
                        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  ASSERT_NE(nullptr, transport_socket_options);

  auto objects = transport_socket_options->downstreamSharedFilterStateObjects();
  EXPECT_EQ(1, objects.size());
  EXPECT_EQ(DownstreamNetworkNamespace::key(), objects.at(0).name_);

  // Verify we can retrieve the network namespace from the filter state object.
  const auto* network_namespace_state =
      dynamic_cast<const DownstreamNetworkNamespace*>(objects.at(0).data_.get());
  ASSERT_NE(nullptr, network_namespace_state);
  EXPECT_EQ(network_namespace_filepath, network_namespace_state->value());
}

TEST_F(TransportSocketOptionsImplTest, NetworkNamespaceDynamicObject) {
  setFilterStateObject(DownstreamNetworkNamespace::key(), "/var/run/netns/development");

  // When network namespace is set alone without shared flag, it won't create transport socket
  // options.
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(nullptr, transport_socket_options);
}

} // namespace
} // namespace Network
} // namespace Envoy
