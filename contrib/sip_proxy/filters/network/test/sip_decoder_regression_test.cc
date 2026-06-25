#include "source/common/buffer/buffer_impl.h"
#include "contrib/sip_proxy/filters/network/source/decoder.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "test/test_common/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace Envoy;
using namespace Envoy::Extensions::NetworkFilters::SipProxy;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

class SipDecoderRegressionTest : public testing::Test {
public:
  SipDecoderRegressionTest() : decoder_(callbacks_) {}

  NiceMock<MockDecoderCallbacks> callbacks_;
  Decoder decoder_;
};

/**
 * Prove that clen/full_msg_len leakage across messages in one buffer prevents
 * processing of subsequent messages that lack a Content-Length header.
 */
TEST_F(SipDecoderRegressionTest, ReassembleMultipleMessagesResetBug) {
  // Message 1: Has Content-Length: 10 and a 10-byte body.
  const std::string SIP_MSG1 =
      "INVITE sip:user@example.com SIP/2.0\x0d\x0a"
      "Content-Length: 10\x0d\x0a"
      "\x0d\x0a"
      "0123456789";

  // Message 2: No Content-Length (empty body).
  const std::string SIP_MSG2 =
      "REGISTER sip:user@example.com SIP/2.0\x0d\x0a"
      "\x0d\x0a";

  Buffer::OwnedImpl buffer;
  buffer.add(SIP_MSG1);
  buffer.add(SIP_MSG2);

  // We expect TWO calls to newDecoderEventHandler.
  // On buggy code, only 1 call happens because the second message reuses clen=10.
  EXPECT_CALL(callbacks_, newDecoderEventHandler(_)).Times(2);

  decoder_.reassemble(buffer);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
