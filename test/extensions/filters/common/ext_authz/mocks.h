#pragma once

#include <string>
#include <vector>

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
  ~MockClient();

  // ExtAuthz::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD3(check, void(RequestCallbacks& callbacks,
                           const envoy::service::auth::v2alpha::CheckRequest& request,
                           Tracing::Span& parent_span));
};

class MockRequestCallbacks : public RequestCallbacks {
public:
  MockRequestCallbacks();
  ~MockRequestCallbacks();

  void onComplete(ResponsePtr&& response) override { onComplete_(response); }

  MOCK_METHOD1(onComplete_, void(ResponsePtr& response));
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
