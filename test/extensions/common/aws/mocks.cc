#include "test/extensions/common/aws/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MockMetadataFetcher::MockMetadataFetcher() {
  // Allow cancel() to be called 0 or more times to handle destructor calls
  EXPECT_CALL(*this, cancel()).Times(testing::AtLeast(0));
}

MockMetadataFetcher::~MockMetadataFetcher() = default;

MockCredentialsProvider::MockCredentialsProvider() = default;

MockCredentialsProvider::~MockCredentialsProvider() = default;

MockSigner::MockSigner() = default;

MockSigner::~MockSigner() = default;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
