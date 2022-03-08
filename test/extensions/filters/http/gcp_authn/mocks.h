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
// class MockGcpAuthnClient : public GcpAuthnClient {
// public:
//   MockGcpAuthnClient(envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig&
//   config,
//                      Server::Configuration::MockFactoryContext& context)
//       : GcpAuthnClient(config, context){};
//   // MockGcpAuthnClient() {};
//   ~MockGcpAuthnClient() override = default;

//   MOCK_METHOD(void, fetchToken, (), ());

//   MOCK_METHOD(void, handleFailure, (), ());
// };

class MockRequestCallbacks : public RequestCallbacks {
public:
  MockRequestCallbacks() = default;
  ~MockRequestCallbacks() override = default;

  void onComplete(ResponseStatus status) override { onComplete_(status); }
  MOCK_METHOD(void, onComplete_, (ResponseStatus status));
};

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
