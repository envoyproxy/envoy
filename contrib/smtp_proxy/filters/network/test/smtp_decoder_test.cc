#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Define fixture class with decoder and mock callbacks.
class SmtpProxyDecoderTestBase {
public:
  SmtpProxyDecoderTestBase() { decoder_ = std::make_unique<DecoderImpl>(); }

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
  data_.drain(data_.length());

  data_.add(std::string(1022, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  EXPECT_EQ(Decoder::Command::UNKNOWN, command.verb);
  EXPECT_EQ(std::string(1022, 'q'), command.raw_verb);
  EXPECT_TRUE(command.rest.empty());
  EXPECT_EQ(1024, command.wire_len);
  data_.drain(data_.length());

  data_.add(std::string(1023, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());

  data_.add("fOo bar\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());
  EXPECT_EQ(9, command.wire_len);
  EXPECT_EQ(Decoder::Command::UNKNOWN, command.verb);
  EXPECT_EQ("fOo", command.raw_verb);
  EXPECT_EQ("bar", command.rest);

  data_.add("eHlO example.com\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeCommand(data_, command));
  data_.drain(data_.length());
  EXPECT_EQ(18, command.wire_len);
  EXPECT_EQ(Decoder::Command::EHLO, command.verb);
  EXPECT_EQ("eHlO", command.raw_verb);
  EXPECT_EQ("example.com", command.rest);
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
  data_.add(std::string(1018, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());
  EXPECT_EQ(200, response.code);
  EXPECT_EQ(std::string(1018, 'q') + "\r\n", response.msg);
  EXPECT_EQ(1024, response.wire_len);

  data_.add("200 ");
  data_.add(std::string(1019, 'q'));
  data_.add("\r\n");
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  // exactly 64KiB response is accepted
  {
    std::string max_line = absl::StrCat("200-", std::string(1018, 'q'), "\r\n");
    ASSERT_EQ(max_line.size(), 1024);
    for (int i = 0; i < 63; ++i) {
      data_.add(max_line);
    }
    max_line[3] = ' ';
    data_.add(max_line);
    EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
    data_.drain(data_.length());
    EXPECT_EQ(200, response.code);
    EXPECT_EQ(65536, response.wire_len);
  }

  // > 64KiB response is rejected
  {
    std::string max_line = absl::StrCat("200-", std::string(1018, 'q'), "\r\n");
    ASSERT_EQ(max_line.size(), 1024);
    for (int i = 0; i < 64; ++i) {
      data_.add(max_line);
    }
    max_line[3] = ' ';
    data_.add(max_line);
    EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
    data_.drain(data_.length());
  }

  data_.add("200 ok\r\n");
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());
  EXPECT_EQ(200, response.code);
  EXPECT_EQ("ok\r\n", response.msg);
  EXPECT_EQ(8, response.wire_len);

  data_.add("qqq \r\n"); // isdigit
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("123q\r\n"); // space or dash
  EXPECT_EQ(Decoder::Result::Bad, decoder_->DecodeResponse(data_, response));
  data_.drain(data_.length());

  data_.add("123-q\r\n321 q\r\n"); // codes must match
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

TEST_F(SmtpProxyDecoderTest, HasCap) {
  std::string caps = "200-example.com at your service\r\n"
                     "200-SMTPUTF8\r\n"
                     "200-PIPELINING\r\n"
                     "200 STARTTLS\r\n";
  EXPECT_TRUE(decoder_->HasEsmtpCapability("smtputf8", caps));
  EXPECT_TRUE(decoder_->HasEsmtpCapability("PIPELINING", caps));
  EXPECT_TRUE(decoder_->HasEsmtpCapability("StArTtLs", caps));
  EXPECT_FALSE(decoder_->HasEsmtpCapability("SIZE", caps));

  caps = "200-STARTTLS is the server greeting, not a capability\r\n"
         "200 PIPELINING\r\n";
  EXPECT_FALSE(decoder_->HasEsmtpCapability("STARTTLS", caps));

  caps = "200-example.com smtp server at your service\r\n"
         "200 STARTTLSXYZ\r\n";
  EXPECT_FALSE(decoder_->HasEsmtpCapability("STARTTLS", caps));

  caps = "200-example.com smtp server at your service\r\n"
         "200 SIZE 1048576\r\n";
  EXPECT_TRUE(decoder_->HasEsmtpCapability("SIZE", caps));
}

TEST_F(SmtpProxyDecoderTest, RemoveCap) {
  std::string caps = "200 example.com at your service\r\n";
  decoder_->RemoveEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200 example.com at your service\r\n", caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200 PIPELINING\r\n";
  decoder_->RemoveEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200 PIPELINING\r\n",
            caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200-STARTTLS\r\n"
         "200 PIPELINING\r\n";
  decoder_->RemoveEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200 PIPELINING\r\n",
            caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200-PIPELINING\r\n"
         "200 STARTTLS\r\n";
  decoder_->RemoveEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200 PIPELINING\r\n",
            caps);

  caps = "200-STARTTLS is the server greeting, not a capability\r\n"
         "200 PIPELINING\r\n";
  decoder_->RemoveEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-STARTTLS is the server greeting, not a capability\r\n"
            "200 PIPELINING\r\n",
            caps);
}

TEST_F(SmtpProxyDecoderTest, AddCap) {
  std::string caps = "200 example.com at your service\r\n";
  decoder_->AddEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200 STARTTLS\r\n",
            caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200 PIPELINING\r\n";
  decoder_->AddEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200-PIPELINING\r\n"
            "200 STARTTLS\r\n",
            caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200-STARTTLS\r\n"
         "200 PIPELINING\r\n";
  decoder_->AddEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200-STARTTLS\r\n"
            "200 PIPELINING\r\n",
            caps);

  caps = "200-example.com at your service\r\n"
         "200-SMTPUTF8\r\n"
         "200-PIPELINING\r\n"
         "200 STARTTLS\r\n";
  decoder_->AddEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-example.com at your service\r\n"
            "200-SMTPUTF8\r\n"
            "200-PIPELINING\r\n"
            "200 STARTTLS\r\n",
            caps);

  caps = "200-STARTTLS is the server greeting, not a capability\r\n"
         "200 PIPELINING\r\n";
  decoder_->AddEsmtpCapability("STARTTLS", caps);
  EXPECT_EQ("200-STARTTLS is the server greeting, not a capability\r\n"
            "200-PIPELINING\r\n"
            "200 STARTTLS\r\n",
            caps);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
