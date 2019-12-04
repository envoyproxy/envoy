#include "common/buffer/buffer_impl.h"

#include "gtest/gtest.h"
#include "library/common/buffer/bridge_fragment.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Buffer {

void envoy_test_release(void* context) {
  uint32_t* counter = static_cast<uint32_t*>(context);
  *counter = *counter + 1;
}

envoy_data toTestEnvoyData(std::string& s, uint32_t* sentinel) {
  return {s.size(), reinterpret_cast<const uint8_t*>(s.c_str()), envoy_test_release, sentinel};
}

TEST(BridgeFragmentTest, Basic) {
  uint32_t* sentinel = new uint32_t;
  *sentinel = 0;
  std::string s = "test string";
  envoy_data c_data = toTestEnvoyData(s, sentinel);

  BridgeFragment* fragment = BridgeFragment::createBridgeFragment(c_data);
  OwnedImpl buffer_wrapper;
  buffer_wrapper.addBufferFragment(*fragment);

  ASSERT_EQ(buffer_wrapper.length(), c_data.length);
  ASSERT_EQ(buffer_wrapper.toString(), s);

  ASSERT_EQ(*sentinel, 0);
  buffer_wrapper.drain(buffer_wrapper.length());
  ASSERT_EQ(*sentinel, 1);
  delete sentinel;
}

} // namespace Buffer
} // namespace Envoy
