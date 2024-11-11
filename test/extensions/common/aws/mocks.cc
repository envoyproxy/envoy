#include "test/extensions/common/aws/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MockCredentialsProvider::MockCredentialsProvider() = default;

MockCredentialsProvider::~MockCredentialsProvider() = default;

MockSigner::MockSigner() = default;

MockSigner::~MockSigner() = default;

MockIAMRolesAnywhereCredentialsProvider::~MockIAMRolesAnywhereCredentialsProvider() = default;

MockX509CredentialsProvider::MockX509CredentialsProvider() = default;

MockX509CredentialsProvider::~MockX509CredentialsProvider() = default;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
