#pragma once

#include "gmock/gmock.h"
#include "library/common/system/system_helper.h"

namespace Envoy {
namespace test {

// Mock implementation of SystemHelper.
class MockSystemHelper : public SystemHelper {
public:
  MockSystemHelper();

  // SystemHelper:
  MOCK_METHOD(bool, isCleartextPermitted, (absl::string_view hostname));
  MOCK_METHOD(envoy_cert_validation_result, validateCertificateChain,
              (const std::vector<std::string>& certs, absl::string_view hostname));
  MOCK_METHOD(void, cleanupAfterCertificateValidation, ());
};

// SystemHelperPeer allows the replacement of the SystemHelper singleton
// with a MockSystemHelper.
class SystemHelperPeer {
public:
  class Handle;

  // Replaces the SystemHelper singleton with a new MockSystemHelper which is
  // wrapped in a Handle. The MockSystemHelper can be accessed via the
  // Handle's `mock_helper()` accessor.
  static std::unique_ptr<Handle> replaceSystemHelper() { return std::make_unique<Handle>(); }

  // RAII type for replacing the SystemHelper singleton with the MockSystemHelper.
  // When this object is destroyed, it resets the SystemHelper singleton back
  // to the previous state.
  class Handle {
  public:
    Handle() {
      previous_ = new test::MockSystemHelper();
      std::swap(SystemHelper::instance_, previous_);
    }

    ~Handle() {
      delete SystemHelper::instance_;
      SystemHelper::instance_ = previous_;
    }

    test::MockSystemHelper& mock_helper() {
      return *static_cast<test::MockSystemHelper*>(SystemHelper::instance_);
    }

  private:
    SystemHelper* previous_;
  };
};

} // namespace test
} // namespace Envoy
