#pragma once

#include <string>
#include <vector>

#include "envoy/ext_authz/ext_authz.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ExtAuthz {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // ExtAuthz::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD3(check, void(RequestCallbacks& callbacks,
                           const envoy::service::auth::v2::CheckRequest& request,
                           Tracing::Span& parent_span));
};

} // namespace ExtAuthz
} // namespace Envoy
