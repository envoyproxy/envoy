#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Define fixture class with decoder and mock callbacks.
class SmtpProxyDecoderTestBase {
public:
  SmtpProxyDecoderTestBase() {
    decoder_ = std::make_unique<DecoderImpl>();
  }

protected:
  std::unique_ptr<DecoderImpl> decoder_;

  Buffer::OwnedImpl data_;
  char buf_[256]{};
  std::string payload_;
};

class SmtpProxyDecoderTest : public SmtpProxyDecoderTestBase, public ::testing::Test {};


TEST_F(SmtpProxyDecoderTest, DecodeCommand) {
  Decoder::Command command;
  EXPECT_EQ(Decoder::Result::NeedMoreData, decoder_->DecodeCommand(data_, command));

  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());;

  data_.add(std::string(510, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  EXPECT_EQ(Decoder::Command::UNKNOWN, command.verb);
  EXPECT_EQ(std::string(510, 'q'), command.raw_verb);
  EXPECT_TRUE(command.rest.empty());
  EXPECT_EQ(512, command.wire_len);
  data_.drain(data_.length());;

  data_.add(std::string(511, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());;

  data_.add("fOo bar\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());
  EXPECT_EQ(9, command.wire_len);
  EXPECT_EQ(Decoder::Command::UNKNOWN, command.verb);
  EXPECT_EQ("fOo", command.raw_verb);
  EXPECT_EQ("bar", command.rest);

  data_.add("eHlO gargantua1\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());
  EXPECT_EQ(17, command.wire_len);
  EXPECT_EQ(Decoder::Command::EHLO, command.verb);
  EXPECT_EQ("eHlO", command.raw_verb);
  EXPECT_EQ("gargantua1", command.rest);
}


TEST_F(SmtpProxyDecoderTest, DecodeResponse) {
  Decoder::Response response;
  EXPECT_EQ(Decoder::Result::NeedMoreData, decoder_->DecodeResponse(data_, response));

  data_.add("200");
  EXPECT_EQ(Decoder::Result::NeedMoreData, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("200\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());
  EXPECT_EQ(200, response.code);
  EXPECT_EQ("", response.msg);
  EXPECT_EQ(5, response.wire_len);

  data_.add("200 ");
  data_.add(std::string(506, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());
  EXPECT_EQ(200, response.code);
  EXPECT_EQ(std::string(506, 'q') + "\r\n", response.msg);
  EXPECT_EQ(512, response.wire_len);

  data_.add("200 ");
  data_.add(std::string(507, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("200 ok\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());
  EXPECT_EQ(200, response.code);
  EXPECT_EQ("ok\r\n", response.msg);
  EXPECT_EQ(8, response.wire_len);

  data_.add("qqq \r\n");  // isdigit
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("123q\r\n");  // space or dash
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("123-q\r\n321 q\r\n");  // codes must match
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("200 ok\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  EXPECT_EQ(200, response.code);
  EXPECT_EQ("ok\r\n", response.msg);
  EXPECT_EQ(8, response.wire_len);

  data_.add("400-bad\r\n");
  data_.add("400 more bad\r\n");
  data_.add("200 pipelined response\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));

  EXPECT_EQ(400, response.code);
  EXPECT_EQ("bad\r\nmore bad\r\n", response.msg);
  EXPECT_EQ(23, response.wire_len);
  data_.drain(response.wire_len);

  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  EXPECT_EQ(200, response.code);
  EXPECT_EQ("pipelined response\r\n", response.msg);
  EXPECT_EQ(24, response.wire_len);
  data_.drain(response.wire_len);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
