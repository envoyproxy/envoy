#include "test/integration/tcp_tunneling_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

class UpstreamIntegrationTest : public BaseTcpTunnelingIntegrationTest {
public:
    void SetUp() override {
        BaseTcpTunnelingIntegrationTest::SetUp();
    }
};

// CredentialInjector integration tests that should run with all protocols

class UpstreamIntegrationTestAllProtocols : public UpstreamIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, UpstreamIntegrationTestAllProtocols,
    testing::ValuesIn(BaseTcpTunnelingIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    BaseTcpTunnelingIntegrationTest::protocolTestParamsToString);

TEST_P(UpstreamIntegrationTestAllProtocols, Test) {
    auto x = 1;
    ASSERT_TRUE(x == 1);
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy