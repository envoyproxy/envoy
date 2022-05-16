#include "test/mocks/ssl/mocks.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

class TestMatchingData {
public:
  TestMatchingData() : ssl_(std::make_shared<Ssl::MockConnectionInfo>()) {}

  static absl::string_view name() { return "ssl"; }

  Ssl::ConnectionInfoConstSharedPtr ssl() const { return ssl_; }

  std::shared_ptr<Ssl::MockConnectionInfo> ssl_;
};

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
