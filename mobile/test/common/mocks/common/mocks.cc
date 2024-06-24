#include "test/common/mocks/common/mocks.h"

namespace Envoy {
namespace test {

using testing::_;
using testing::Return;

MockSystemHelper::MockSystemHelper() {
  ON_CALL(*this, isCleartextPermitted(_)).WillByDefault(Return(true));
  envoy_cert_validation_result success;
  success.result = ENVOY_SUCCESS;
  success.tls_alert = 0;
  success.error_details = "";
  ON_CALL(*this, validateCertificateChain(_, _)).WillByDefault(Return(success));
}

} // namespace test
} // namespace Envoy
