#include "envoy/http/filter.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/filters/common/rbac/matching/data_impl.h"
#include "source/extensions/filters/common/rbac/matching/inputs.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

TEST(MatchingData, DestinationIPInput) {
  Network::Matching::DestinationIPInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    info.downstream_connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    info.downstream_connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, DestinationPortInput) {
  Network::Matching::DestinationPortInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    info.downstream_connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "8080");
  }

  {
    info.downstream_connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, SourceIPInput) {
  Network::Matching::SourceIPInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    info.downstream_connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    info.downstream_connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, SourcePortInput) {
  Network::Matching::SourcePortInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    info.downstream_connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "8080");
  }

  {
    info.downstream_connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, DirectSourceIPInput) {
  Network::Matching::DirectSourceIPInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST(MatchingData, ServerNameInput) {
  Network::Matching::ServerNameInput<MatchingData> input;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);

  {
    EXPECT_CALL(conn, requestedServerName).WillOnce(testing::Return(""));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    const auto host = "example.com";
    EXPECT_CALL(conn, requestedServerName).WillOnce(testing::Return(host));
    const auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, host);
  }
}

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
