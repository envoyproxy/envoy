#include "test/mocks/aws/mocks.h"

namespace Envoy {
namespace Aws {
namespace Auth {

MockCredentialsProvider::MockCredentialsProvider() {}
MockCredentialsProvider::~MockCredentialsProvider() {}

MockRegionProvider::MockRegionProvider() {}
MockRegionProvider::~MockRegionProvider() {}

MockSigner::MockSigner() {}
MockSigner::~MockSigner() {}

MockMetadataFetcher::MockMetadataFetcher() {}
MockMetadataFetcher::~MockMetadataFetcher() {}

} // namespace Auth
} // namespace Aws
} // namespace Envoy