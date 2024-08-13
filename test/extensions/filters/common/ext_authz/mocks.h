#pragma once

#include <string>
#include <vector>

#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/extensions/filters/common/ext_authz/ext_authz.h"

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
  MOCK_METHOD(void, check,
              (RequestCallbacks & callbacks, const envoy::service::auth::v3::CheckRequest& request,
               Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info));
  MOCK_METHOD(StreamInfo::StreamInfo const*, streamInfo, (), (const));
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
