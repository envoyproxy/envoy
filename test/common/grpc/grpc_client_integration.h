#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/common/assert.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

// Support parameterizing over state-of-the-world xDS vs delta xDS.
enum class SotwOrDelta { Sotw, Delta, UnifiedSotw, UnifiedDelta };

// Unified or Legacy grpc mux implementation
enum class LegacyOrUnified { Legacy, Unified };

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
      PANIC("reached unexpected code");
    }
  }
};

class GrpcClientIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, ClientType>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, ClientType>>& p) {
    return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
};

// TODO(kbaichoo): Revert to using GrpcClientIntegrationParamTest for all
// derived classes when deferred processing is enabled by default. It's
// parameterized by deferred processing now to avoid bit rot since the feature
// is off by default.
class GrpcClientIntegrationParamTestWithDeferredProcessing
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>> {
public:
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<std::tuple<Network::Address::IpVersion, Grpc::ClientType>, bool>>& p) {
    return fmt::format(
        "{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(std::get<0>(p.param))),
        std::get<1>(std::get<0>(p.param)) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                          : "EnvoyGrpc",
        std::get<1>(p.param) ? "WithDeferredProcessing" : "NoDeferredProcessing");
  }
  Network::Address::IpVersion ipVersion() const override {
    return std::get<0>(std::get<0>(GetParam()));
  }
  Grpc::ClientType clientType() const override { return std::get<1>(std::get<0>(GetParam())); }
  bool deferredProcessing() const { return std::get<1>(GetParam()); }
};

class UnifiedOrLegacyMuxIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, ClientType, LegacyOrUnified>> {
public:
  ~UnifiedOrLegacyMuxIntegrationParamTest() override = default;
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion, ClientType, LegacyOrUnified>>& p) {
    return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
                       std::get<2>(p.param) == LegacyOrUnified::Legacy ? "Legacy" : "Unified");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
  LegacyOrUnified unifiedOrLegacy() const { return std::get<2>(GetParam()); }
  bool isUnified() const { return std::get<2>(GetParam()) == LegacyOrUnified::Unified; }
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
    return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
                       std::get<2>(p.param) == SotwOrDelta::Delta ? "Delta" : "StateOfTheWorld");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
  SotwOrDelta sotwOrDelta() const { return std::get<2>(GetParam()); }
};

class DeltaSotwDeferredClustersIntegrationParamTest
    : public BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, ClientType, SotwOrDelta, bool>> {
public:
  ~DeltaSotwDeferredClustersIntegrationParamTest() override = default;
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion, ClientType, SotwOrDelta, bool>>& p) {
    return fmt::format("{}_{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                       std::get<1>(p.param) == ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
                       std::get<2>(p.param) == SotwOrDelta::Delta ? "Delta" : "StateOfTheWorld",
                       std::get<3>(p.param) == true ? "DeferredClusters" : "");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  ClientType clientType() const override { return std::get<1>(GetParam()); }
  SotwOrDelta sotwOrDelta() const { return std::get<2>(GetParam()); }
  bool useDeferredCluster() const { return std::get<3>(GetParam()); }
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

#define GRPC_CLIENT_INTEGRATION_PARAMS                                                             \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()))
#define GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS                                         \
  testing::Combine(GRPC_CLIENT_INTEGRATION_PARAMS, testing::Bool())
#define DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS                                                  \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta))
#define DELTA_SOTW_GRPC_CLIENT_DEFERRED_CLUSTERS_INTEGRATION_PARAMS                                \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta),             \
                   testing::Values(true, false))
#define UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS                                              \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::LegacyOrUnified::Legacy, Grpc::LegacyOrUnified::Unified))
#define DELTA_SOTW_UNIFIED_GRPC_CLIENT_INTEGRATION_PARAMS                                          \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta,              \
                                   Grpc::SotwOrDelta::UnifiedSotw,                                 \
                                   Grpc::SotwOrDelta::UnifiedDelta))

} // namespace Grpc
} // namespace Envoy
