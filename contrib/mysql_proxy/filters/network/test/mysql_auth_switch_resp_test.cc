#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

TEST(MySQLAuthSwithRespTest, AuthSwithResp) {
  ClientSwitchResponse switch_resp_encode{};
  switch_resp_encode.setAuthPluginResp(MySQLTestUtils::getAuthResp8());
  Buffer::OwnedImpl decode_data;
  switch_resp_encode.encode(decode_data);

  ClientSwitchResponse switch_resp_decode{};
  switch_resp_decode.decode(decode_data, AUTH_SWITH_RESP_SEQ, decode_data.length());
  EXPECT_EQ(switch_resp_encode.getAuthPluginResp(), switch_resp_decode.getAuthPluginResp());
}

TEST(MySQLAuthSwithRespTest, AuthSwithRespErrLengthResp) {
  ClientSwitchResponse switch_resp_encode{};
  switch_resp_encode.setAuthPluginResp(MySQLTestUtils::getAuthResp8());
  Buffer::OwnedImpl buffer;
  switch_resp_encode.encode(buffer);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), 0);
  ClientSwitchResponse switch_resp_decode{};
  switch_resp_decode.decode(decode_data, AUTH_SWITH_RESP_SEQ, -1);
  EXPECT_EQ(switch_resp_decode.getAuthPluginResp().size(), 0);
}
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
