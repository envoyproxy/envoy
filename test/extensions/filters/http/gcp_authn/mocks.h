#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class MockGcpAuthnClientCallbacks : public GcpAuthnClient::Callbacks {
public:
  MockGcpAuthnClientCallbacks() = default;
  ~MockGcpAuthnClientCallbacks() override = default;

  MOCK_METHOD(void, onComplete, (absl::StatusOr<GcpToken> token));
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
