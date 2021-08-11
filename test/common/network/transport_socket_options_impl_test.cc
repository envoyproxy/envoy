#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class TransportSocketOptionsImplTest : public testing::Test {
public:
  TransportSocketOptionsImplTest()
      : filter_state_(StreamInfo::FilterState::LifeSpan::FilterChain) {}

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

} // namespace
} // namespace Network
} // namespace Envoy
