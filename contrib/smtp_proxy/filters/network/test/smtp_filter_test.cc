#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tuple>

#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_test_utils.h"

using ConfigProto = envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

using ::testing::ReturnRef;
using ::testing::Truly;
using ::testing::WithArgs;

// Fixture class.
class SmtpFilterTest : public ::testing::Test {
public:
  SmtpFilterTest() {
    Configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                        /*downstream_ssl=*/ConfigProto::ENABLE,
                                                        /*upstream_ssl=*/ConfigProto::DISABLE});
  }

  void SetUp() override {
    EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  }

  void Configure(const SmtpFilterConfig::SmtpFilterConfigOptions& config_options) {
    config_ = std::make_shared<SmtpFilterConfig>(config_options, scope_);
    filter_ = std::make_unique<SmtpFilter>(config_);

    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void setMetadata() {
    EXPECT_CALL(read_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    ON_CALL(stream_info_, setDynamicMetadata(NetworkFilterNames::get().SmtpProxy, _))
        .WillByDefault(Invoke([this](const std::string&, const ProtobufWkt::Struct& obj) {
          stream_info_.metadata_.mutable_filter_metadata()->insert(
              Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
                  NetworkFilterNames::get().SmtpProxy, obj));
        }));
  }

  void ExpectInjectReadData(absl::string_view s, bool end_stream) {
    EXPECT_CALL(read_callbacks_,
                injectReadDataToFilterChain(
                    Truly([s](Buffer::Instance& b) { return b.toString() == s; }), end_stream));
  }

  void ExpectInjectWriteData(absl::string_view s, bool end_stream) {
    EXPECT_CALL(write_callbacks_,
                injectWriteDataToFilterChain(
                    Truly([s](Buffer::Instance& b) { return b.toString() == s; }), end_stream));
  }

  void ReadExpectConsumed(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(buf, false));
    EXPECT_EQ(0, buf.length());
  }

  void ReadExpectContinue(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(buf, false));
    EXPECT_EQ(s.size(), buf.length());
  }

  void WriteExpectConsumed(absl::string_view s) {
    Buffer::OwnedImpl buf(s);
    EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onWrite(buf, false));
    EXPECT_EQ(0, buf.length());
  }

  void WriteExpectContinue(absl::string_view s) {
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

// TODO other flows:
// proxy protocol on/off

TEST_F(SmtpFilterTest, BadPipeline) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  ExpectInjectWriteData("503 bad pipeline\r\n", true);
  EXPECT_CALL(connection_, close(_));

  ReadExpectConsumed("ehlo gargantua1\r\nstuff");
  ASSERT_THAT(filter_->getStats().sessions_.value(), 1);
  ASSERT_THAT(filter_->getStats().sessions_bad_pipeline_.value(), 1);
}

// !(ehlo or helo)
TEST_F(SmtpFilterTest, NoHelo) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  ExpectInjectWriteData("503 bad sequence of commands\r\n", true);
  EXPECT_CALL(connection_, close(_));

  ReadExpectConsumed("starttls\r\n");
  ASSERT_THAT(filter_->getStats().sessions_bad_ehlo_.value(), 1);
}

TEST_F(SmtpFilterTest, NoEsmtp) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view helo = "helo client.com\r\n";
  ExpectInjectReadData(helo, false);
  ReadExpectConsumed(helo);

  absl::string_view ehlo_resp = "220 upstream smtp server at your service\r\n";
  WriteExpectContinue(ehlo_resp);

  ReadExpectContinue("mail from:<alice>\r\n");
  WriteExpectContinue("200 ok\r\n");
  ASSERT_THAT(filter_->getStats().sessions_non_esmtp_.value(), 1);
}

TEST_F(SmtpFilterTest, NoStarttls) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo gargantua1\r\n";
  ExpectInjectReadData(ehlo_command, false);
  ReadExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200 PIPELINING\r\n";
  absl::string_view updated_ehlo_response = "200-upstream smtp server at your service\r\n"
                                            "200-PIPELINING\r\n"
                                            "200 STARTTLS\r\n";
  ExpectInjectWriteData(updated_ehlo_response, false);
  WriteExpectConsumed(ehlo_response);

  absl::string_view kMail = "mail from:<someone>\r\n";
  ExpectInjectReadData(kMail, false);
  ReadExpectConsumed(kMail);

  ASSERT_THAT(filter_->getStats().sessions_esmtp_unencrypted_.value(), 1);
}

TEST_F(SmtpFilterTest, DownstreamStarttls) {
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo gargantua1\r\n";
  ExpectInjectReadData(ehlo_command, false);
  ReadExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200 PIPELINING\r\n";
  absl::string_view updated_ehlo_response = "200-upstream smtp server at your service\r\n"
                                            "200-PIPELINING\r\n"
                                            "200 STARTTLS\r\n";
  ExpectInjectWriteData(updated_ehlo_response, false);
  WriteExpectConsumed(ehlo_response);

  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));

  absl::string_view kStarttlsResp = "220 envoy ready for tls\r\n";
  ExpectInjectWriteData(kStarttlsResp, false); // starttls response

  ReadExpectConsumed("starttls\r\n");

  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  cb(kStarttlsResp.size());

  // PASSTHROUGH after downstream STARTTLS

  const absl::string_view kEhlo2 = "ehlo gargantua1\r\n";
  ReadExpectContinue(kEhlo2);

  WriteExpectContinue("200-upstream smtp ok\r\n"
                      "200-PIPELINING\r\n"
                      "200 8BITMIME\r\n");

  ReadExpectContinue("mail from:<someone>\r\n");

  ASSERT_THAT(filter_->getStats().sessions_downstream_terminated_ssl_.value(), 1);
}

// downstream startUpstreamSecureTransport() err
// downstream REQUIRE + not invoked

TEST_F(SmtpFilterTest, UpstreamStarttls) {
  Configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::DISABLE,
                                                      /*upstream_ssl=*/ConfigProto::ENABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo gargantua1\r\n";
  ExpectInjectReadData(ehlo_command, false);
  ReadExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  absl::string_view updated_ehlo_response = "200-upstream smtp server at your service\r\n"
                                            "200 PIPELINING\r\n";
  ExpectInjectReadData("STARTTLS\r\n", false);
  WriteExpectConsumed(ehlo_response);

  EXPECT_CALL(read_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));
  ExpectInjectWriteData(updated_ehlo_response, false);
  WriteExpectConsumed("200 upstream ready for tls\r\n");

  // PASSTHROUGH after upstream STARTTLS

  const absl::string_view kEhlo2 = "ehlo gargantua1\r\n";
  ReadExpectContinue(kEhlo2);

  WriteExpectContinue("200-upstream smtp ok\r\n"
                      "200-PIPELINING\r\n"
                      "200 8BITMIME\r\n");

  ReadExpectContinue("mail from:<someone>\r\n");

  ASSERT_THAT(filter_->getStats().sessions_upstream_terminated_ssl_.value(), 1);
}

TEST_F(SmtpFilterTest, UpstreamStarttlsErr) {
  Configure(SmtpFilterConfig::SmtpFilterConfigOptions{stat_prefix_,
                                                      /*downstream_ssl=*/ConfigProto::DISABLE,
                                                      /*upstream_ssl=*/ConfigProto::ENABLE});

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  ExpectInjectWriteData("220 upstream ESMTP\r\n", false);
  WriteExpectConsumed("220 upstream ESMTP\r\n");

  absl::string_view ehlo_command = "ehlo gargantua1\r\n";
  ExpectInjectReadData(ehlo_command, false);
  ReadExpectConsumed(ehlo_command);

  absl::string_view ehlo_response = "200-upstream smtp server at your service\r\n"
                                    "200-PIPELINING\r\n"
                                    "200 STARTTLS\r\n";
  ExpectInjectReadData("STARTTLS\r\n", false);
  WriteExpectConsumed(ehlo_response);

  ExpectInjectWriteData("400 upstream STARTTLS error\r\n", true);
  EXPECT_CALL(connection_, close(_));
  WriteExpectConsumed("400 ENOMEM\r\n");

  ASSERT_THAT(filter_->getStats().sessions_upstream_ssl_command_err_.value(), 1);
}

// upstream startUpstreamSecureTransport() err
// upstream REQUIRE + not offered

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
