#include "extensions/filters/http/common/jwks_fetcher.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

class MockJwksFetcher : public JwksFetcher {
public:
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD2(fetch, void(const ::envoy::api::v2::core::HttpUri& uri, JwksReceiver& receiver));
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
