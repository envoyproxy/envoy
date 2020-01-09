#pragma once

#include <string>
#include <vector>

#include "envoy/service/auth/v3alpha/external_auth.pb.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient() override;

  // ExtAuthz::Client
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD3(check, void(RequestCallbacks& callbacks,
                           const envoy::service::auth::v3alpha::CheckRequest& request,
                           Tracing::Span& parent_span));
};

class MockRequestCallbacks : public RequestCallbacks {
public:
  MockRequestCallbacks();
  ~MockRequestCallbacks() override;

  void onComplete(ResponsePtr&& response) override { onComplete_(response); }

  MOCK_METHOD(void, onComplete_, (ResponsePtr & response));
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
