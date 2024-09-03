#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/dynamic_modules/abi_version.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// This test ensures that abi_version.h contains the correct sha256 hash of ABI header files.
TEST(DynamicModules, ABIVersionCheck) {
  const auto abi_header_path =
      TestEnvironment::substitute("{{ test_rundir }}/source/extensions/dynamic_modules/abi.h");
  // Read the header file and calculate the sha256 hash.
  const std::string abi_header = TestEnvironment::readFileToStringForTest(abi_header_path);
  const std::string sha256 =
      Hex::encode(Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(
          Buffer::OwnedImpl(abi_header)));
  EXPECT_EQ(sha256, kAbiVersion);
}

// This test ensures that the copied ABI header files in the SDK directory are identical to the
// original ABI headers.
TEST(DynamicModules, ABIHeaderIdentical) {
  const std::string original_abi_header = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/source/extensions/dynamic_modules/abi.h"));
  const std::string rust_abi_header =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/source/extensions/dynamic_modules/sdk/rust/abi.h"));
  EXPECT_EQ(original_abi_header, rust_abi_header);
  // TODO: Add Go SDK.
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
