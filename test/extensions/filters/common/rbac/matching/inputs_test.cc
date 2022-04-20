#include "envoy/http/filter.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/filters/common/rbac/matching/data_impl.h"
#include "source/extensions/filters/common/rbac/matching/inputs.h"

#include "test/mocks/network/mocks.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

class InputsTest : public testing::Test {
public:
  InputsTest()
      : data_(conn_, headers_, info_),
        provider_(std::make_shared<Network::Address::Ipv4Instance>(80),
                  std::make_shared<Network::Address::Ipv4Instance>(80)) {
    ON_CALL(conn_, connectionInfoProvider()).WillByDefault(ReturnRef(provider_));
  }

  MatchingDataImpl data_;
  NiceMock<Envoy::Network::MockConnection> conn_;
  Envoy::Http::TestRequestHeaderMapImpl headers_;
  NiceMock<StreamInfo::MockStreamInfo> info_;

  Network::ConnectionInfoSetterImpl provider_;
};

TEST_F(InputsTest, DestinationIPInput) {
  Network::Matching::DestinationIPInput<MatchingData> input;

  {
    provider_.setLocalAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    provider_.setLocalAddress(std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, DestinationPortInput) {
  Network::Matching::DestinationPortInput<MatchingData> input;

  {
    provider_.setLocalAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "8080");
  }

  {
    provider_.setLocalAddress(std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, SourceIPInput) {
  Network::Matching::SourceIPInput<MatchingData> input;

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, SourcePortInput) {
  Network::Matching::SourcePortInput<MatchingData> input;

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "8080");
  }

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, DirectSourceIPInput) {
  Network::Matching::DirectSourceIPInput<MatchingData> input;

  {
    provider_.setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "127.0.0.1");
  }

  {
    provider_.setDirectRemoteAddressForTest(
        std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, SourceTypeInput) {
  Network::Matching::SourceTypeInput<MatchingData> input;

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, "local");
  }

  {
    provider_.setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }
}

TEST_F(InputsTest, ServerNameInput) {
  Network::Matching::ServerNameInput<MatchingData> input;

  {
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    const auto host = "example.com";
    provider_.setRequestedServerName(host);
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, host);
  }
}

TEST_F(InputsTest, HttpRequestHeadersInput) {
  Http::Matching::HttpRequestHeadersDataInput<MatchingData> input("header");

  {
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, absl::nullopt);
  }

  {
    const auto header = "foo";
    headers_.setByKey("header", header);
    const auto result = input.get(data_);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.data_, header);
  }
}

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy