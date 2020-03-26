#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/network/address_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/check_request_utils.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class CheckRequestUtilsTest : public testing::Test {
public:
  CheckRequestUtilsTest() {
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    protocol_ = Envoy::Http::Protocol::Http10;
    buffer_ = CheckRequestUtilsTest::newTestBuffer(8192);
    ssl_ = std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>();
  };

  void expectBasicHttp(int required_count = 1) {
    const int times = required_count * 2;
    EXPECT_CALL(callbacks_, connection()).Times(times).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(connection_, remoteAddress())
        .Times(required_count)
        .WillRepeatedly(ReturnRef(addr_));
    EXPECT_CALL(connection_, localAddress()).Times(required_count).WillRepeatedly(ReturnRef(addr_));
    EXPECT_CALL(Const(connection_), ssl()).Times(times).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(callbacks_, streamId()).Times(required_count).WillRepeatedly(Return(0));
    EXPECT_CALL(callbacks_, decodingBuffer())
        .Times(required_count)
        .WillRepeatedly(Return(buffer_.get()));
    EXPECT_CALL(callbacks_, streamInfo())
        .Times(required_count)
        .WillRepeatedly(ReturnRef(req_info_));
    EXPECT_CALL(req_info_, protocol()).Times(times).WillRepeatedly(ReturnPointee(&protocol_));
    EXPECT_CALL(req_info_, startTime()).Times(required_count).WillRepeatedly(Return(SystemTime()));
  }

  void
  callHttpCheckAndValidateRequestAttributes(const Http::TestRequestHeaderMapImpl& request_headers,
                                            bool include_peer_certificate,
                                            const std::string& expected_scheme = "") {
    envoy::service::auth::v3::CheckRequest request;
    Protobuf::Map<std::string, std::string> context_extensions;
    context_extensions["key"] = "value";

    envoy::config::core::v3::Metadata metadata_context;
    auto metadata_val = MessageUtil::keyValueStruct("foo", "bar");
    (*metadata_context.mutable_filter_metadata())["meta.key"] = metadata_val;

    CheckRequestUtils::createHttpCheck(&callbacks_, request_headers, std::move(context_extensions),
                                       std::move(metadata_context), request, false,
                                       include_peer_certificate);

    EXPECT_EQ(expected_scheme, request.attributes().request().http().scheme());
    EXPECT_EQ("source", request.attributes().source().principal());
    EXPECT_EQ("destination", request.attributes().destination().principal());
    EXPECT_EQ("foo", request.attributes().source().service());
    EXPECT_EQ("value", request.attributes().context_extensions().at("key"));
    EXPECT_EQ("bar", request.attributes()
                         .metadata_context()
                         .filter_metadata()
                         .at("meta.key")
                         .fields()
                         .at("foo")
                         .string_value());

    if (include_peer_certificate) {
      EXPECT_EQ(cert_data_, request.attributes().source().certificate());
    } else {
      EXPECT_EQ(0, request.attributes().source().certificate().size());
    }
  }

  static Buffer::InstancePtr newTestBuffer(uint64_t size) {
    auto buffer = std::make_unique<Buffer::OwnedImpl>();
    while (buffer->length() < size) {
      auto new_buffer =
          Buffer::OwnedImpl("Lorem ipsum dolor sit amet, consectetuer adipiscing elit.");
      buffer->add(new_buffer);
    }
    return buffer;
  }

  Network::Address::InstanceConstSharedPtr addr_;
  absl::optional<Http::Protocol> protocol_;
  CheckRequestUtils check_request_generator_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Envoy::Network::MockReadFilterCallbacks> net_callbacks_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  std::shared_ptr<NiceMock<Envoy::Ssl::MockConnectionInfo>> ssl_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info_;
  Buffer::InstancePtr buffer_;
  const std::string cert_data_{"cert-data"};
  const Http::TestRequestHeaderMapImpl request_headers_{
      {"x-envoy-downstream-service-cluster", "foo"}, {":path", "/bar"}};
};

// Verify that createTcpCheck's dependencies are invoked when it's called.
// Verify that the source certificate is not set by default.
TEST_F(CheckRequestUtilsTest, BasicTcp) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(2).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, false);

  EXPECT_EQ(request.attributes().source().certificate().size(), 0);
}

// Verify that createTcpCheck's dependencies are invoked when it's called.
// Verify that createTcpCheck populates the source certificate correctly.
TEST_F(CheckRequestUtilsTest, TcpPeerCertificate) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(2).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  EXPECT_CALL(*ssl_, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(cert_data_));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, true);

  EXPECT_EQ(cert_data_, request.attributes().source().certificate());
}

// Verify that createHttpCheck's dependencies are invoked when it's called.
// Verify that check request object has no request data.
// Verify that a client supplied EnvoyAuthPartialBody will not affect the
// CheckRequest call.
TEST_F(CheckRequestUtilsTest, BasicHttp) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  // A client supplied EnvoyAuthPartialBody header should be ignored.
  Http::TestRequestHeaderMapImpl request_headers{
      {Http::Headers::get().EnvoyAuthPartialBody.get(), "1"}};

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(&callbacks_, request_headers,
                                     Protobuf::Map<std::string, std::string>(),
                                     envoy::config::core::v3::Metadata(), request_, size, false);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_EQ(request_.attributes().request().http().headers().end(),
            request_.attributes().request().http().headers().find(
                Http::Headers::get().EnvoyAuthPartialBody.get()));
  EXPECT_TRUE(request_.attributes().request().has_time());
}

// Verify that check request object has only a portion of the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithPartialBody) {
  const uint64_t size = 4049;
  Http::RequestHeaderMapImpl headers_;
  envoy::service::auth::v3::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(&callbacks_, headers_,
                                     Protobuf::Map<std::string, std::string>(),
                                     envoy::config::core::v3::Metadata(), request_, size, false);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_EQ("true", request_.attributes().request().http().headers().at(
                        Http::Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has all the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithFullBody) {
  Http::RequestHeaderMapImpl headers_;
  envoy::service::auth::v3::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers_, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), request_, buffer_->length(), false);
  ASSERT_EQ(buffer_->length(), request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, buffer_->length()),
            request_.attributes().request().http().body());
  EXPECT_EQ("false", request_.attributes().request().http().headers().at(
                         Http::Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that createHttpCheck extract the proper attributes from the http request into CheckRequest
// proto object.
// Verify that the source certificate is not set by default.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeer) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-downstream-service-cluster", "foo"},
                                                 {":path", "/bar"}};
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillRepeatedly(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(callbacks_, streamId()).WillRepeatedly(Return(0));
  EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(callbacks_, decodingBuffer()).Times(1);
  EXPECT_CALL(req_info_, protocol()).WillRepeatedly(ReturnPointee(&protocol_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  callHttpCheckAndValidateRequestAttributes(request_headers_, false);
}

// Verify that createHttpCheck extract the attributes from the HTTP request into CheckRequest
// proto object and URI SAN is used as principal if present.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerUriSans) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  callHttpCheckAndValidateRequestAttributes(request_headers_, false);
}

// Verify that createHttpCheck extract the attributes from the HTTP request into CheckRequest
// proto object and DNS SAN is used as principal if URI SAN is absent.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerDnsSans) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*ssl_, dnsSansPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));

  EXPECT_CALL(*ssl_, uriSanLocalCertificate()).WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*ssl_, dnsSansLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  Protobuf::Map<std::string, std::string> context_extensions;
  context_extensions["key"] = "value";

  callHttpCheckAndValidateRequestAttributes(request_headers_, false);
}

// Verify that createHttpCheck extract the attributes from the HTTP request into CheckRequest
// proto object and Subject is used as principal if both URI SAN and DNS SAN are absent.
TEST_F(CheckRequestUtilsTest, CheckAttrContextSubject) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*ssl_, dnsSansPeerCertificate()).WillOnce(Return(std::vector<std::string>{}));
  std::string subject_peer = "source";
  EXPECT_CALL(*ssl_, subjectPeerCertificate()).WillOnce(ReturnRef(subject_peer));

  EXPECT_CALL(*ssl_, uriSanLocalCertificate()).WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(*ssl_, dnsSansLocalCertificate()).WillOnce(Return(std::vector<std::string>{}));
  std::string subject_local = "destination";
  EXPECT_CALL(*ssl_, subjectLocalCertificate()).WillOnce(ReturnRef(subject_local));

  callHttpCheckAndValidateRequestAttributes(request_headers_, false);
}

// Verify that the source certificate is populated correctly.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerCertificate) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  EXPECT_CALL(*ssl_, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(cert_data_));

  callHttpCheckAndValidateRequestAttributes(request_headers_, true);
}

// Verify that the http request scheme attribute is populated correctly.
TEST_F(CheckRequestUtilsTest, CheckScheme) {
  expectBasicHttp(4);

  EXPECT_CALL(*ssl_, uriSanPeerCertificate())
      .WillRepeatedly(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillRepeatedly(Return(std::vector<std::string>{"destination"}));
  EXPECT_CALL(*ssl_, urlEncodedPemEncodedPeerCertificate()).WillRepeatedly(ReturnRef(cert_data_));

  // This request has scheme entry.
  const Http::TestRequestHeaderMapImpl request_headers_scheme_http{
      {":scheme", "https"}, {"x-envoy-downstream-service-cluster", "foo"}, {":path", "/bar"}};
  callHttpCheckAndValidateRequestAttributes(request_headers_scheme_http, false, "https");

  // This request has empty scheme entry, but it has x-forwarded-proto set.
  const Http::TestRequestHeaderMapImpl request_headers_xfp_http{
      {"x-forwarded-proto", "http"},
      {"x-envoy-downstream-service-cluster", "foo"},
      {":path", "/bar"}};
  callHttpCheckAndValidateRequestAttributes(request_headers_xfp_http, false, "http");

  // This request has both scheme and x-forwarded-proto set.
  const Http::TestRequestHeaderMapImpl request_headers_scheme_xfp_http{
      {":scheme", "https"},
      {"x-forwarded-proto", "http"},
      {"x-envoy-downstream-service-cluster", "foo"},
      {":path", "/bar"}};
  callHttpCheckAndValidateRequestAttributes(request_headers_scheme_xfp_http, false, "http");

  // This request has both scheme and x-forwarded-proto set. However, the x-forwarded-proto is set
  // to empty.
  const Http::TestRequestHeaderMapImpl request_headers_scheme_xfp_empty_http{
      {":scheme", "https"},
      {"x-forwarded-proto", ""},
      {"x-envoy-downstream-service-cluster", "foo"},
      {":path", "/bar"}};
  callHttpCheckAndValidateRequestAttributes(request_headers_scheme_xfp_empty_http, false, "https");
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
