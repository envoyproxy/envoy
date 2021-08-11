#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/common/assert.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

// Support parameterizing over state-of-the-world xDS vs delta xDS.
enum class SotwOrDelta { Sotw, Delta };

class BaseGrpcClientIntegrationParamTest {
public:
  virtual ~BaseGrpcClientIntegrationParamTest() = default;
  virtual Network::Address::IpVersion ipVersion() const PURE;
  virtual ClientType clientType() const PURE;

  void setGrpcService(envoy::config::core::v3::GrpcService& grpc_service,
                      const std::string& cluster_name,
                      Network::Address::InstanceConstSharedPtr address) {
    // Set a 5 minute timeout to avoid flakes. If this causes a real test timeout the test is
    // broken and/or should be using simulated time.
    grpc_service.mutable_timeout()->CopyFrom(Protobuf::util::TimeUtil::SecondsToDuration(300));
    switch (clientType()) {
    case ClientType::EnvoyGrpc:
      grpc_service.mutable_envoy_grpc()->set_cluster_name(cluster_name);
      break;
    case ClientType::GoogleGrpc: {
      auto* google_grpc = grpc_service.mutable_google_grpc();
      google_grpc->set_target_uri(address->asString());
      google_grpc->set_stat_prefix(cluster_name);
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

class GrpcClientIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, ClientType>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, ClientType>>& p) {
    return fmt::format("{}_{}",
                       std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
};

class VersionedGrpcClientIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, ClientType,
                                               envoy::config::core::v3::ApiVersion>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, ClientType,
                                                envoy::config::core::v3::ApiVersion>>& p) {
    return fmt::format("{}_{}_{}",
                       std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
                       ApiVersion_Name(std::get<2>(p.param)));
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
  envoy::config::core::v3::ApiVersion apiVersion() const { return std::get<2>(GetParam()); }
};

class DeltaSotwIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, ClientType, SotwOrDelta>> {
public:
  ~DeltaSotwIntegrationParamTest() override = default;
  static std::string
  protocolTestParamsToString(const ::testing::TestParamInfo<
                             std::tuple<Network::Address::IpVersion, ClientType, SotwOrDelta>>& p) {
    return fmt::format("{}_{}_{}",
                       std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
                       std::get<2>(p.param) == SotwOrDelta::Delta ? "Delta" : "StateOfTheWorld");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
  SotwOrDelta sotwOrDelta() const { return std::get<2>(GetParam()); }
};

// Skip tests based on gRPC client type.
#define SKIP_IF_GRPC_CLIENT(client_type)                                                           \
  if (clientType() == (client_type)) {                                                             \
    return;                                                                                        \
  }

// Skip tests based on xDS delta vs state-of-the-world.
#define SKIP_IF_XDS_IS(xds)                                                                        \
  if (sotwOrDelta() == (xds)) {                                                                    \
    return;                                                                                        \
  }

// For VersionedGrpcClientIntegrationParamTest, skip when testing with
// ENVOY_DISABLE_DEPRECATED_FEATURES.
#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
#define XDS_DEPRECATED_FEATURE_TEST_SKIP                                                           \
  if (apiVersion() != envoy::config::core::v3::ApiVersion::V3) {                                   \
    return;                                                                                        \
  }
#else
#define XDS_DEPRECATED_FEATURE_TEST_SKIP
#endif // ENVOY_DISABLE_DEPRECATED_FEATURES

#define GRPC_CLIENT_INTEGRATION_PARAMS                                                             \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()))
#define VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS                                                   \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(envoy::config::core::v3::ApiVersion::V3,                        \
                                   envoy::config::core::v3::ApiVersion::V2,                        \
                                   envoy::config::core::v3::ApiVersion::AUTO))
#define DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS                                                  \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta))

} // namespace Grpc
} // namespace Envoy
