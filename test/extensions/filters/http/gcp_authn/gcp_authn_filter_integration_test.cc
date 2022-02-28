#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {
namespace {

class GcpAuthnFilterIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public HttpProtocolIntegrationTest {
protected:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  const std::string filter_config_ = R"EOF(
    name: envoy.filters.http.gcp_authn
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig
    )EOF";
};

} // namespace
} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
