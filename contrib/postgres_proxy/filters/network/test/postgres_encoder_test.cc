#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/postgres_proxy/filters/network/source/postgres_encoder.h"
#include "contrib/postgres_proxy/filters/network/source/postgres_message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class EncoderTest : public ::testing::Test {
public:
  EncoderTest() { encoder_ = std::make_unique<Encoder>(); }

protected:
  std::unique_ptr<Encoder> encoder_;
};

TEST_F(EncoderTest, BuildErrorResponseProducesCorrectFormat) {
  const std::string severity = "FATAL";
  const std::string message = "some error";
  const std::string code = "01007";

  Buffer::OwnedImpl data = encoder_->buildErrorResponse(severity, message, code);

  std::unique_ptr<Message> msg = createMsgBodyReader<Byte1, Repeated<String>>();
  // Make sure response sent back to client is valid
  ASSERT_THAT(msg->validate(data, 0, data.length()), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, data.length()));
  auto out = msg->toString();

  ASSERT_TRUE(out.find("[E]") != std::string::npos);
  ASSERT_TRUE(out.find(severity) != std::string::npos);
  ASSERT_TRUE(out.find(message) != std::string::npos);
  ASSERT_TRUE(out.find(code) != std::string::npos);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
