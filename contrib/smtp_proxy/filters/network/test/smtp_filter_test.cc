#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tuple>

#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

using ConfigProto = envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

using ::testing::ReturnRef;
using ::testing::Truly;

// Fixture class.
class SmtpFilterTest : public ::testing::Test {
public:
  SmtpFilterTest() {
    configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                        /*downstream_ssl=*/ConfigProto::ENABLE,
                                                        /*upstream_ssl=*/ConfigProto::DISABLE});
  }

  void SetUp() override {
    EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  }

  void configure(const SmtpFilterConfig::SmtpFilterConfigOptions& config_options) {
    config_ = std::make_shared<SmtpFilterConfig>(config_options, scope_);
    filter_ = std::make_unique<SmtpFilter>(config_);

    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void setMetadata() {
    EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  }

  void expectInjectReadData(absl::string_view s, bool end_stream) {
    EXPECT_CALL(read_callbacks_,
                injectReadDataToFilterChain(
                    Truly([s](Buffer::Instance& b) { return b.toString() == s; }), end_stream));
  }

  void expectInjectWriteData(absl::string_view s, bool end_stream) {
    EXPECT_CALL(write_callbacks_,
                injectWriteDataToFilterChain(
                    Truly([s](Buffer::Instance& b) { return b.toString() == s; }), end_stream));
  }

  void readExpectConsumed(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(buf, false));
    EXPECT_EQ(0, buf.length());
  }

  void readExpectContinue(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(buf, false));
    EXPECT_EQ(s.size(), buf.length());
  }

  void writeExpectConsumed(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onWrite(buf, false));
    EXPECT_EQ(0, buf.length());
  }

  void writeExpectContinue(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(buf, false));
    EXPECT_EQ(s.size(), buf.length());
  }

  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::string stat_prefix_{"test."};
  std::unique_ptr<SmtpFilter> filter_;
  SmtpFilterConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

  // These variables are used internally in tests.
  Buffer::OwnedImpl data_;
  char buf_[256];
};

TEST_F(SmtpFilterTest, BadPipeline) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  expectInjectWriteData("503 bad pipeline\r\n", true);
  EXPECT_CALL(connection_, close(_));

  readExpectConsumed("ehlo example.com\r\nstuff");
  ASSERT_THAT(filter_->getStats().sessions_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_bad_pipeline_.value(), 1);
}

// !(ehlo or helo)
TEST_F(SmtpFilterTest, NoHelo) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  expectInjectWriteData("503 bad sequence of commands\r\n", true);
  EXPECT_CALL(connection_, close(_));

  readExpectConsumed("starttls\r\n");
  ASSERT_THAT(filter_->getStats().sessions_bad_ehlo_.value(), 1);
}

TEST_F(SmtpFilterTest, NoEsmtp) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view helo = "helo example.com\r\n";
  expectInjectReadData(helo, false);
  readExpectConsumed(helo);

  absl::string_view ehlo_resp = "220 upstream smtp server at your service\r\n";
  writeExpectContinue(ehlo_resp);

  readExpectContinue("mail from:<alice>\r\n");
  writeExpectContinue("200 ok\r\n");
  ASSERT_THAT(filter_->getStats().sessions_non_esmtp_.value(), 1);
}

TEST_F(SmtpFilterTest, NoStarttls) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200 PIPELINING\r\n";
  absl::string_view updated_ehlo_response = "200-upstream smtp server at your service\r\n"
                                            "200-PIPELINING\r\n"
                                            "200 STARTTLS\r\n";
  expectInjectWriteData(updated_ehlo_response, false);
  writeExpectConsumed(ehlo_response);

  absl::string_view kMail = "mail from:<someone>\r\n";
  expectInjectReadData(kMail, false);
  readExpectConsumed(kMail);

  ASSERT_THAT(filter_->getStats().sessions_esmtp_unencrypted_.value(), 1);
}

// upstream and downstream starttls disabled: filter should pass through caps and client invocation
TEST_F(SmtpFilterTest, DisabledUpstreamDownstreamTls) {
  configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::DISABLE,
                                                      /*upstream_ssl=*/ConfigProto::DISABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  expectInjectWriteData(ehlo_response, false);
  writeExpectConsumed(ehlo_response);

  readExpectContinue("starttls\r\n");
  writeExpectContinue("220 upstream ready for tls\r\n");

  // Filter now in passthrough state, TLS exchange is opaque/binary data.
  readExpectContinue("\x01\x02\x03\x04");
  writeExpectContinue("\x05\x06\x07\x08");

  ASSERT_THAT(filter_->getStats().sessions_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_non_esmtp_.value(), 0);
  ASSERT_THAT(filter_->getStats().sessions_downstream_terminated_ssl_.value(), 0);
  ASSERT_THAT(filter_->getStats().sessions_upstream_terminated_ssl_.value(), 0);
}

TEST_F(SmtpFilterTest, DownstreamStarttls) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200 PIPELINING\r\n";
  absl::string_view updated_ehlo_response = "200-upstream smtp server at your service\r\n"
                                            "200-PIPELINING\r\n"
                                            "200 STARTTLS\r\n";
  expectInjectWriteData(updated_ehlo_response, false);
  writeExpectConsumed(ehlo_response);

  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));

  absl::string_view kStarttlsResp = "220 envoy ready for tls\r\n";
  expectInjectWriteData(kStarttlsResp, false); // starttls response

  readExpectConsumed("starttls\r\n");

  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  cb(kStarttlsResp.size());

  // PASSTHROUGH after downstream STARTTLS

  const absl::string_view kEhlo2 = "ehlo example.com\r\n";
  readExpectContinue(kEhlo2);

  writeExpectContinue("200-upstream smtp ok\r\n"
                      "200-PIPELINING\r\n"
                      "200 8BITMIME\r\n");

  readExpectContinue("mail from:<someone>\r\n");

  ASSERT_THAT(filter_->getStats().sessions_downstream_terminated_ssl_.value(), 1);
}

// TODO(jsbucy): other flows:
// downstream startUpstreamSecureTransport() err
// downstream REQUIRE + not invoked

TEST_F(SmtpFilterTest, UpstreamStarttls) {
  configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::DISABLE,
                                                      /*upstream_ssl=*/ConfigProto::ENABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  expectInjectReadData("STARTTLS\r\n", false);
  writeExpectConsumed(ehlo_response);

  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));
  expectInjectReadData(ehlo_command, false);
  writeExpectConsumed("200 upstream ready for tls\r\n");

  writeExpectContinue("200-upstream smtp ok\r\n"
                      "200-PIPELINING\r\n"
                      "200 8BITMIME\r\n");

  readExpectContinue("mail from:<someone>\r\n");

  ASSERT_THAT(filter_->getStats().sessions_upstream_terminated_ssl_.value(), 1);
}

TEST_F(SmtpFilterTest, UpstreamStarttlsErr) {
  configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::DISABLE,
                                                      /*upstream_ssl=*/ConfigProto::ENABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  expectInjectReadData("STARTTLS\r\n", false);
  writeExpectConsumed(ehlo_response);

  expectInjectWriteData("400 upstream STARTTLS error\r\n", true);
  EXPECT_CALL(connection_, close(_));
  writeExpectConsumed("400 ENOMEM\r\n");

  ASSERT_THAT(filter_->getStats().sessions_upstream_ssl_command_err_.value(), 1);
}

TEST_F(SmtpFilterTest, DownstreamUpstreamStarttls) {
  configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::ENABLE,
                                                      /*upstream_ssl=*/ConfigProto::ENABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  expectInjectWriteData("220 upstream ESMTP\r\n", false);
  writeExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo example.com\r\n";
  expectInjectReadData(ehlo_command, false);
  readExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  expectInjectReadData("STARTTLS\r\n", false);
  writeExpectConsumed(ehlo_response);

  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));
  expectInjectReadData(ehlo_command, false);
  writeExpectConsumed("200 upstream ready for tls\r\n");

  absl::string_view updated_ehlo_response = "200-upstream smtp ok\r\n"
                                            "200-PIPELINING\r\n"
                                            "200-8BITMIME\r\n"
                                            "200 STARTTLS\r\n";
  expectInjectWriteData(updated_ehlo_response, false);
  writeExpectConsumed("200-upstream smtp ok\r\n"
                      "200-PIPELINING\r\n"
                      "200 8BITMIME\r\n");

  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));

  absl::string_view kStarttlsResp = "220 envoy ready for tls\r\n";
  expectInjectWriteData(kStarttlsResp, false); // starttls response
  readExpectConsumed("STARTTLS\r\n");

  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  cb(kStarttlsResp.size());

  // PASSTHROUGH after downstream STARTTLS
  readExpectContinue("mail from:<alice@example.com>\r\n");

  ASSERT_THAT(filter_->getStats().sessions_downstream_terminated_ssl_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_upstream_terminated_ssl_.value(), 1);
}

// upstream startUpstreamSecureTransport() err
// upstream REQUIRE + not offered

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
