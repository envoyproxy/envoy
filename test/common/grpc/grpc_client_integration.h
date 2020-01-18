#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "common/common/assert.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

// Support parameterizing over gRPC client type.
enum class ClientType { EnvoyGrpc, GoogleGrpc };
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

#ifdef ENVOY_GOOGLE_GRPC
#define GRPC_CLIENT_INTEGRATION_PARAMS                                                             \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::Values(Grpc::ClientType::EnvoyGrpc, Grpc::ClientType::GoogleGrpc))
#define DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS                                                  \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::Values(Grpc::ClientType::EnvoyGrpc, Grpc::ClientType::GoogleGrpc),     \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta))
#else
#define GRPC_CLIENT_INTEGRATION_PARAMS                                                             \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::Values(Grpc::ClientType::EnvoyGrpc))
#define DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS                                                  \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::Values(Grpc::ClientType::EnvoyGrpc),                                   \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta))
#endif // ENVOY_GOOGLE_GRPC

} // namespace Grpc
} // namespace Envoy
