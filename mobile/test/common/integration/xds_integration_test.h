#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/integration/base_client_integration_test.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

static constexpr absl::string_view XDS_CLUSTER = "xds_cluster.lyft.com";

// A base class for xDS integration tests. It provides common functionality for integration tests
// derived from BaseClientIntegrationTest that needs to communicate with upstream xDS servers.
class XdsIntegrationTest : public BaseClientIntegrationTest,
                           public Grpc::DeltaSotwIntegrationParamTest {
public:
  XdsIntegrationTest();
  virtual ~XdsIntegrationTest() = default;

protected:
  void SetUp() override;
  void TearDown() override;

  void createEnvoy() override;

  // Initializes the xDS connection and creates a gRPC bi-directional stream for receiving
  // DiscoveryRequests and sending DiscoveryResponses.
  void initializeXdsStream();

  // Returns the IP version that the test is running with (IPv4 or IPv6).
  Network::Address::IpVersion ipVersion() const override;
  // Returns the gRPC client type that the test is running with (Envoy gRPC or Google gRPC).
  Grpc::ClientType clientType() const override;
  // Returns whether the test is using the state-of-the-world or Delta xDS protocol.
  Grpc::SotwOrDelta sotwOrDelta() const;

  // Get the runtime configuration value for the given key. The runtime value is either statically
  // provided in the bootstrap config or provided (or overridden) by the RTDS config.
  std::string getRuntimeKey(const std::string& key);

  // Creates a cluster config with a single static endpoint, where the endpoint is intended to be of
  // a fake upstream on the loopback address.
  envoy::config::cluster::v3::Cluster
  createSingleEndpointClusterConfig(const std::string& cluster_name);
  // Creates an admin config for being able to query various configuration values.
  envoy::config::bootstrap::v3::Admin adminConfig();

  // Get the value of a Counter in the Envoy instance.
  uint64_t getCounterValue(const std::string& counter);
  // Wait until the Counter specified by `name` is >= `value`.
  ABSL_MUST_USE_RESULT testing::AssertionResult waitForCounterGe(const std::string& name,
                                                                 uint64_t value);

private:
  std::string admin_filename_;
};

} // namespace Envoy
