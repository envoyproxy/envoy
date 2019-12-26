#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/stream_info/filter_state_impl.h"

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
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::make_optional<std::string>("www.example.com"),
            transport_socket_options->serverNameOverride());
  EXPECT_TRUE(transport_socket_options->applicationProtocolListOverride().empty());
}

TEST_F(TransportSocketOptionsImplTest, ApplicationProtocols) {
  std::vector<std::string> http_alpns{"h2", "http/1.1"};
  filter_state_.setData(
      ApplicationProtocols::key(), std::make_unique<ApplicationProtocols>(http_alpns),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  auto transport_socket_options = TransportSocketOptionsUtility::fromFilterState(filter_state_);
  EXPECT_EQ(absl::nullopt, transport_socket_options->serverNameOverride());
  EXPECT_EQ(http_alpns, transport_socket_options->applicationProtocolListOverride());
}

TEST_F(TransportSocketOptionsImplTest, Both) {
  std::vector<std::string> http_alpns{"h2", "http/1.1"};
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
