#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class MockRequestCallbacks : public RequestCallbacks {
public:
  MockRequestCallbacks() = default;
  ~MockRequestCallbacks() override = default;

  MOCK_METHOD(void, onComplete, (const Http::ResponseMessage* response));
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
