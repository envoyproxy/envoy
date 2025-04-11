#include <utility>

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/override_host/selected_hosts.h"

#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {
namespace {

TEST(SelectedHostsTest, ValidProto) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "1.2.3.4:1234" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_TRUE(selected_hosts_result.ok());
  auto selected_hosts = std::move(selected_hosts_result.value());
  EXPECT_EQ(selected_hosts->primary.address.address, "1.2.3.4");
  EXPECT_EQ(selected_hosts->primary.address.port, 1234);
  EXPECT_TRUE(selected_hosts->failover.empty());
}

TEST(SelectedHostsTest, ValidProtoIPv6) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "[1:2:3::4]:1234" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_TRUE(selected_hosts_result.ok());
  auto selected_hosts = std::move(selected_hosts_result.value());
  EXPECT_EQ(selected_hosts->primary.address.address, "1:2:3::4");
  EXPECT_EQ(selected_hosts->primary.address.port, 1234);
  EXPECT_TRUE(selected_hosts->failover.empty());
}

TEST(SelectedHostsTest, ProtoMultipleEndpoints) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "1.2.3.4:1234, 5.6.7.8:5678" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Primary endpoint is not a single address"));
}

TEST(SelectedHostsTest, InvalidProto) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { number_value: 5678 }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                                              "Primary endpoint is not a string"));
}

TEST(SelectedHostsTest, InvalidProtoBadKey) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "some-invalid-key"
      value { string_value: "1.2.3.4:1234" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                                              "Missing primary endpoint"));
}

TEST(SelectedHostsTest, InvalidProtoEmptyAddress) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Primary endpoint is not a single address"));
}

TEST(SelectedHostsTest, InvalidProtoBadAddress) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "i'm not an address" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Address 'i'm not an address' is not in host:port format"));
}

TEST(SelectedHostsTest, ProtoInvalidMultipleEndpoints) {
  Envoy::ProtobufWkt::Struct selected_endpoints;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value { string_value: "1.2.3.4:1234, , 5.6.7.8:5678" }
    }
  )pb",
                                                    &selected_endpoints));
  auto selected_hosts_result = SelectedHosts::make(selected_endpoints);
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument, "Address is empty"));
}

} // namespace
} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
