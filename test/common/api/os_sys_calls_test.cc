#include "source/common/api/os_sys_calls_impl.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(OsSyscallsTest, SetAlternateGetifaddrs) {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const bool pre_alternate_support = os_syscalls.supportsGetifaddrs();
  Api::InterfaceAddressVector interfaces{};
#if defined(WIN32) || (defined(__ANDROID_API__) && __ANDROID_API__ < 24)
  EXPECT_FALSE(pre_alternate_support);
  EXPECT_DEATH(os_syscalls.getifaddrs(interfaces), "not implemented");
#else
  EXPECT_TRUE(pre_alternate_support);
  const auto pre_alternate_rc = os_syscalls.getifaddrs(interfaces);
  EXPECT_EQ(0, pre_alternate_rc.return_value_);
  EXPECT_FALSE(interfaces.empty());
#endif

  os_syscalls.setAlternateGetifaddrs(
      [](Api::InterfaceAddressVector& interfaces) -> Api::SysCallIntResult {
        interfaces.emplace_back("made_up_if", 0, nullptr);
        return {0, 0};
      });
  interfaces.clear();

  const bool post_alternate_support = os_syscalls.supportsGetifaddrs();
  EXPECT_TRUE(post_alternate_support);
  EXPECT_EQ(0, os_syscalls.getifaddrs(interfaces).return_value_);
  EXPECT_EQ(1, interfaces.size());
  EXPECT_EQ("made_up_if", interfaces.front().interface_name_);
}
} // namespace Envoy
