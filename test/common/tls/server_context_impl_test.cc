#include "source/common/tls/server_context_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
TEST(ServerContextImplTest, OneByteCBSParameterReturnsEmptyCurveNIDVector) {
  // A one-byte CBS will result in `CBS_get_u16` returning false
  // giving us coverage for the break in `getClientCurveNIDSupported`.
  const uint8_t* data = reinterpret_cast<const uint8_t*>("a");
  CBS cbs;
  CBS_init(&cbs, data, 1);
  Ssl::CurveNIDVector nullCbs = getClientCurveNIDSupported(cbs);
  EXPECT_EQ(0, nullCbs.size());
}
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
