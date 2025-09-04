#include "test/extensions/common/aws/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MockMetadataFetcher::MockMetadataFetcher() = default;

MockMetadataFetcher::~MockMetadataFetcher() = default;

MockCredentialsProvider::MockCredentialsProvider() = default;

MockCredentialsProvider::~MockCredentialsProvider() = default;

MockSigner::MockSigner() = default;

MockSigner::~MockSigner() = default;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
