#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

// error: constructor for 'Envoy::Extensions::HttpFilters::GcpAuthentication::MockGcpAuthnClient'
// must explicitly initialize the base class
// 'Envoy::Extensions::HttpFilters::GcpAuthentication::GcpAuthnClient' which does not have a default
// constructor MockGcpAuthnClient(Server::Configuration::MockFactoryContext&,
class MockGcpAuthnClient : public GcpAuthnClient {
public:
  MockGcpAuthnClient(Server::Configuration::MockFactoryContext&,
                     envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig&){};
  ~MockGcpAuthnClient() override = default;

  MOCK_METHOD(Envoy::Http::RequestMessagePtr, buildRequest,
              (const std::string& method, const std::string& server_url), (const));
};

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy