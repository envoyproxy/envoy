#include "contrib/postgres_proxy/filters/network/source/postgres_encoder.h"
#include "contrib/postgres_proxy/filters/network/source/postgres_message.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>


namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class EncoderImplTest : public ::testing::Test {
public:
    EncoderImplTest() {
        encoder_ = std::make_unique<EncoderImpl>();
    }
protected:
  std::unique_ptr<EncoderImpl> encoder_;
};

TEST_F(EncoderImplTest, BuildErrorResponseProducesCorrectFormat) {
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
