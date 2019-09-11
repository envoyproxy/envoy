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
namespace {

class CheckRequestUtilsTest : public testing::Test {
public:
  CheckRequestUtilsTest() {
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    protocol_ = Envoy::Http::Protocol::Http10;
    buffer_ = CheckRequestUtilsTest::newTestBuffer(8192);
    ssl_ = std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>();
  };

  void expectBasicHttp() {
    EXPECT_CALL(callbacks_, connection()).Times(2).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
    EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
    EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(callbacks_, streamId()).Times(1).WillOnce(Return(0));
    EXPECT_CALL(callbacks_, decodingBuffer()).WillOnce(Return(buffer_.get()));
    EXPECT_CALL(callbacks_, streamInfo()).Times(3).WillRepeatedly(ReturnRef(req_info_));
    EXPECT_CALL(req_info_, protocol()).Times(2).WillRepeatedly(ReturnPointee(&protocol_));
  }

  void callHttpCheckAndValidateRequestAttributes() {
    Http::TestHeaderMapImpl request_headers{{"x-envoy-downstream-service-cluster", "foo"},
                                            {":path", "/bar"}};
    envoy::service::auth::v2::CheckRequest request;
    Protobuf::Map<std::string, std::string> context_extensions;
    context_extensions["key"] = "value";

    envoy::api::v2::core::Metadata metadata_context;
    auto metadata_val = MessageUtil::keyValueStruct("foo", "bar");
    (*metadata_context.mutable_filter_metadata())["meta.key"] = metadata_val;

    CheckRequestUtils::createHttpCheck(&callbacks_, request_headers, std::move(context_extensions),
                                       std::move(metadata_context), request, false);

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
};

// Verify that createTcpCheck's dependencies are invoked when it's called.
TEST_F(CheckRequestUtilsTest, BasicTcp) {
  envoy::service::auth::v2::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(2).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request);
}

// Verify that createHttpCheck's dependencies are invoked when it's called.
// Verify that check request object has no request data.
// Verify that a client supplied EnvoyAuthPartialBody will not affect the
// CheckRequest call.
TEST_F(CheckRequestUtilsTest, BasicHttp) {
  const uint64_t size = 0;
  envoy::service::auth::v2::CheckRequest request_;

  // A client supplied EnvoyAuthPartialBody header should be ignored.
  Http::TestHeaderMapImpl request_headers{{Http::Headers::get().EnvoyAuthPartialBody.get(), "1"}};

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(&callbacks_, request_headers,
                                     Protobuf::Map<std::string, std::string>(),
                                     envoy::api::v2::core::Metadata(), request_, size);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_EQ(request_.attributes().request().http().headers().end(),
            request_.attributes().request().http().headers().find(
                Http::Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has only a portion of the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithPartialBody) {
  const uint64_t size = 4049;
  Http::HeaderMapImpl headers_;
  envoy::service::auth::v2::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(&callbacks_, headers_,
                                     Protobuf::Map<std::string, std::string>(),
                                     envoy::api::v2::core::Metadata(), request_, size);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_EQ("true", request_.attributes().request().http().headers().at(
                        Http::Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has all the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithFullBody) {
  Http::HeaderMapImpl headers_;
  envoy::service::auth::v2::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(&callbacks_, headers_,
                                     Protobuf::Map<std::string, std::string>(),
                                     envoy::api::v2::core::Metadata(), request_, buffer_->length());
  ASSERT_EQ(buffer_->length(), request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, buffer_->length()),
            request_.attributes().request().http().body());
  EXPECT_EQ("false", request_.attributes().request().http().headers().at(
                         Http::Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that createHttpCheck extract the proper attributes from the http request into CheckRequest
// proto object.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeer) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-downstream-service-cluster", "foo"},
                                          {":path", "/bar"}};
  envoy::service::auth::v2::CheckRequest request;
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

  callHttpCheckAndValidateRequestAttributes();
}

// Verify that createHttpCheck extract the attributes from the HTTP request into CheckRequest
// proto object and URI SAN is used as principal if present.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerUriSans) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  callHttpCheckAndValidateRequestAttributes();
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

  callHttpCheckAndValidateRequestAttributes();
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

  callHttpCheckAndValidateRequestAttributes();
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
