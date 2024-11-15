#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/ext_authz/check_request_utils.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"

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
    EXPECT_CALL(callbacks_, connection())
        .Times(2)
        .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
    EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(callbacks_, streamId()).WillOnce(Return(0));
    EXPECT_CALL(callbacks_, decodingBuffer()).WillOnce(Return(buffer_.get()));
    EXPECT_CALL(callbacks_, streamInfo()).WillOnce(ReturnRef(req_info_));
    EXPECT_CALL(req_info_, protocol()).Times(2).WillRepeatedly(ReturnPointee(&protocol_));
    EXPECT_CALL(req_info_, startTime()).WillOnce(Return(SystemTime()));
  }

  void callHttpCheckAndValidateRequestAttributes(
      bool include_peer_certificate,
      const envoy::service::auth::v3::AttributeContext_TLSSession* want_tls_session) {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-downstream-service-cluster", "foo"},
                                                   {":path", "/bar"}};
    envoy::service::auth::v3::CheckRequest request;
    Protobuf::Map<std::string, std::string> context_extensions;
    context_extensions["key"] = "value";
    Protobuf::Map<std::string, std::string> labels;
    labels["label_1"] = "value_1";
    labels["label_2"] = "value_2";

    envoy::config::core::v3::Metadata metadata_context;
    auto metadata_val = MessageUtil::keyValueStruct("foo", "bar");
    (*metadata_context.mutable_filter_metadata())["meta.key"] = metadata_val;

    CheckRequestUtils::createHttpCheck(
        &callbacks_, request_headers, std::move(context_extensions), std::move(metadata_context),
        envoy::config::core::v3::Metadata(), request, /*max_request_bytes=*/0,
        /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, include_peer_certificate,
        want_tls_session != nullptr, labels, nullptr, nullptr);

    EXPECT_EQ("source", request.attributes().source().principal());
    EXPECT_EQ("destination", request.attributes().destination().principal());
    EXPECT_EQ("foo", request.attributes().source().service());
    EXPECT_EQ("value", request.attributes().context_extensions().at("key"));
    EXPECT_EQ("value_1", request.attributes().destination().labels().at("label_1"));
    EXPECT_EQ("value_2", request.attributes().destination().labels().at("label_2"));
    EXPECT_EQ("bar", request.attributes()
                         .metadata_context()
                         .filter_metadata()
                         .at("meta.key")
                         .fields()
                         .at("foo")
                         .string_value());
    EXPECT_TRUE(request.attributes().has_route_metadata_context());

    if (include_peer_certificate) {
      EXPECT_EQ(cert_data_, request.attributes().source().certificate());
    } else {
      EXPECT_EQ(0, request.attributes().source().certificate().size());
    }

    EXPECT_EQ(want_tls_session != nullptr, request.attributes().has_tls_session());
    if (want_tls_session != nullptr) {
      if (!want_tls_session->sni().empty()) {
        EXPECT_EQ(want_tls_session->sni(), request.attributes().tls_session().sni());
      } else {
        EXPECT_EQ(requested_server_name_, request.attributes().tls_session().sni());
      }
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

  MatcherSharedPtr createRequestHeaderAllowlist() {
    NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_autz_proto_;
    ext_autz_proto_.mutable_allowed_headers()->add_patterns()->set_exact("allowed");
    ext_autz_proto_.mutable_allowed_headers()->add_patterns()->set_exact("allowed-dupe");
    ext_autz_proto_.mutable_allowed_headers()->add_patterns()->set_exact("allowed-and-disallowed");
    return CheckRequestUtils::toRequestMatchers(ext_autz_proto_.allowed_headers(), false,
                                                factory_context);
  }

  MatcherSharedPtr createRequestHeaderDenylist() {
    NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz ext_autz_proto_;
    ext_autz_proto_.mutable_disallowed_headers()->add_patterns()->set_exact("disallowed");
    ext_autz_proto_.mutable_disallowed_headers()->add_patterns()->set_exact(
        "allowed-and-disallowed");
    return CheckRequestUtils::toRequestMatchers(ext_autz_proto_.disallowed_headers(), false,
                                                factory_context);
  }

  static void expectHeadersInHeaderMap(
      const envoy::config::core::v3::HeaderMap& header_map,
      const std::vector<std::pair<absl::string_view, absl::string_view>>& expected_headers) {
    for (const auto& [key, value] : expected_headers) {
      bool found = false;
      for (const auto& header : header_map.headers()) {
        if (header.key() == key && header.raw_value() == value) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << fmt::format("did not find expected header key/value pair: '{}': '{}'",
                                        key, value);
    }
  }

  static void expectHeadersNotInHeaderMap(
      const envoy::config::core::v3::HeaderMap& header_map,
      const std::vector<std::pair<absl::string_view, absl::string_view>>& unexpected_headers) {
    for (const auto& [key, value] : unexpected_headers) {
      bool found = false;
      for (const auto& header : header_map.headers()) {
        if (header.key() == key && header.raw_value() == value) {
          found = true;
          break;
        }
      }
      EXPECT_FALSE(found) << fmt::format("found unexpected header key/value pair: '{}': '{}'", key,
                                         value);
    }
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
  const std::string sni_{"foo.example.org"};
  const absl::string_view requested_server_name_{"server.name"};
};

// Verify that createTcpCheck's dependencies are invoked when it's called.
// Verify that the source certificate is not set by default.
TEST_F(CheckRequestUtilsTest, BasicTcp) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(3).WillRepeatedly(ReturnRef(connection_));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(connection_, requestedServerName()).WillOnce(Return(requested_server_name_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  Protobuf::Map<std::string, std::string> labels;
  labels["label_1"] = "value_1";
  labels["label_2"] = "value_2";
  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, false, false, labels);

  EXPECT_EQ(request.attributes().source().certificate().size(), 0);
  EXPECT_EQ("value_1", request.attributes().destination().labels().at("label_1"));
  EXPECT_EQ("value_2", request.attributes().destination().labels().at("label_2"));
  EXPECT_EQ("server.name", request.attributes().destination().service());
}

// Verify that createTcpCheck's dependencies are invoked when it's called.
// Verify that createTcpCheck populates the source certificate correctly.
TEST_F(CheckRequestUtilsTest, TcpPeerCertificate) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(3).WillRepeatedly(ReturnRef(connection_));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  EXPECT_CALL(*ssl_, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(cert_data_));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, true, false,
                                    Protobuf::Map<std::string, std::string>());

  EXPECT_EQ(cert_data_, request.attributes().source().certificate());
}

// Verify that createTcpCheck populates the tls session details correctly.
TEST_F(CheckRequestUtilsTest, TcpTlsSession) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(4).WillRepeatedly(ReturnRef(connection_));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(Const(connection_), ssl()).Times(3).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  envoy::service::auth::v3::AttributeContext_TLSSession want_tls_session;
  want_tls_session.set_sni(sni_);
  EXPECT_CALL(*ssl_, sni()).Times(2).WillRepeatedly(ReturnRef(want_tls_session.sni()));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, false, true,
                                    Protobuf::Map<std::string, std::string>());
  EXPECT_TRUE(request.attributes().has_tls_session());
  EXPECT_EQ(want_tls_session.sni(), request.attributes().tls_session().sni());
}

// Verify that createTcpCheck populates the tls session details correctly from the connection when
// TLS session information isn't present.
TEST_F(CheckRequestUtilsTest, TcpTlsSessionNoSessionSni) {
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(net_callbacks_, connection()).Times(4).WillRepeatedly(ReturnRef(connection_));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(connection_, requestedServerName())
      .Times(3)
      .WillRepeatedly(Return(requested_server_name_));
  EXPECT_CALL(Const(connection_), ssl()).Times(3).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  envoy::service::auth::v3::AttributeContext_TLSSession want_tls_session;
  EXPECT_CALL(*ssl_, sni()).WillOnce(ReturnRef(want_tls_session.sni()));

  CheckRequestUtils::createTcpCheck(&net_callbacks_, request, false, true,
                                    Protobuf::Map<std::string, std::string>());
  EXPECT_TRUE(request.attributes().has_tls_session());
  EXPECT_EQ(requested_server_name_, request.attributes().tls_session().sni());
}

// Verify that createHttpCheck's dependencies are invoked when it's called.
// Verify that check request object has no request data.
// Verify that a client supplied EnvoyAuthPartialBody will not affect the
// CheckRequest call.
TEST_F(CheckRequestUtilsTest, BasicHttp) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  // A client supplied EnvoyAuthPartialBody header should be ignored.
  Http::TestRequestHeaderMapImpl request_headers{{Headers::get().EnvoyAuthPartialBody.get(), "1"}};

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(
      &callbacks_, request_headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(), nullptr, nullptr);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());
  EXPECT_EQ(request_.attributes().request().http().headers().end(),
            request_.attributes().request().http().headers().find(
                Headers::get().EnvoyAuthPartialBody.get()));
  EXPECT_TRUE(request_.attributes().request().has_time());
}

// Verify that check request merges the duplicate headers.
TEST_F(CheckRequestUtilsTest, BasicHttpWithDuplicateHeaders) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  // A client supplied duplicate header should be merged.
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-duplicate-header", ""}, {"x-duplicate-header", "foo"}, {"x-duplicate-header", "bar"},
      {"x-normal-header", "foo"}, {"x-empty-header", ""},
  };

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(
      &callbacks_, request_headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(), nullptr, nullptr);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());
  EXPECT_EQ(",foo,bar", request_.attributes().request().http().headers().at("x-duplicate-header"));
  EXPECT_EQ("foo", request_.attributes().request().http().headers().at("x-normal-header"));
  EXPECT_EQ("", request_.attributes().request().http().headers().at("x-empty-header"));
  EXPECT_TRUE(request_.attributes().request().has_time());
}

// Verify that check request only contains allowlisted headers,
// and that duplicate headers are merged.
TEST_F(CheckRequestUtilsTest, BasicHttpWithRequestHeaderAllowlist) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  Http::TestRequestHeaderMapImpl request_headers{
      {"allowed", "allowed value"},
      {"allowed-dupe", "one"},
      {"allowed-dupe", "two"},
      {"not-allowed", "not explicitly allowed"},
  };

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();

  CheckRequestUtils::createHttpCheck(
      &callbacks_, request_headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(),
      createRequestHeaderAllowlist(), nullptr);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());

  EXPECT_TRUE(request_.attributes().request().http().headers().contains("allowed"));
  EXPECT_EQ("allowed value", request_.attributes().request().http().headers().at("allowed"));

  // No denylist was used.
  EXPECT_TRUE(request_.attributes().request().http().headers().contains("allowed-dupe"));
  EXPECT_EQ("one,two", request_.attributes().request().http().headers().at("allowed-dupe"));

  EXPECT_FALSE(request_.attributes().request().http().headers().contains("not-allowed"));
}

// Verify that check request only contains allowlisted headers,
// and that duplicate headers are merged.
TEST_F(CheckRequestUtilsTest, BasicHttpWithRequestHeaderDenylist) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  Http::TestRequestHeaderMapImpl request_headers{
      {"not-disallowed", "not disallowed value"},
      {"not-disallowed-dupe", "one"},
      {"not-disallowed-dupe", "two"},
      {"disallowed", "disallowed value"},
  };

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();

  CheckRequestUtils::createHttpCheck(
      &callbacks_, request_headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(), nullptr,
      createRequestHeaderDenylist());
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());

  ASSERT_TRUE(request_.attributes().request().http().headers().contains("not-disallowed-dupe"));
  EXPECT_EQ("one,two", request_.attributes().request().http().headers().at("not-disallowed-dupe"));

  ASSERT_TRUE(request_.attributes().request().http().headers().contains("not-disallowed"));
  EXPECT_EQ("not disallowed value",
            request_.attributes().request().http().headers().at("not-disallowed"));

  EXPECT_FALSE(request_.attributes().request().http().headers().contains("disallowed"));
}

// Verify that check request only contains allowlisted headers,
// and that duplicate headers are merged.
TEST_F(CheckRequestUtilsTest, BasicHttpWithRequestHeaderAllowlistAndDenylist) {
  const uint64_t size = 0;
  envoy::service::auth::v3::CheckRequest request_;

  Http::TestRequestHeaderMapImpl request_headers{
      {"allowed", "allowed value"},
      {"allowed-dupe", "one"},
      {"allowed-dupe", "two"},
      {"disallowed", "disallowed value"},
      {"allowed-and-disallowed", "allowed and disallowed value"},
      {"not-allowed-or-disallowed", "not allowed or disallowed value"},
  };

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();

  CheckRequestUtils::createHttpCheck(
      &callbacks_, request_headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(),
      createRequestHeaderAllowlist(), createRequestHeaderDenylist());
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());

  ASSERT_TRUE(request_.attributes().request().http().headers().contains("allowed"));
  EXPECT_EQ("allowed value", request_.attributes().request().http().headers().at("allowed"));

  ASSERT_TRUE(request_.attributes().request().http().headers().contains("allowed-dupe"));
  EXPECT_EQ("one,two", request_.attributes().request().http().headers().at("allowed-dupe"));

  EXPECT_FALSE(request_.attributes().request().http().headers().contains("disallowed"));
  EXPECT_FALSE(request_.attributes().request().http().headers().contains("allowed-and-disallowed"));
  EXPECT_FALSE(
      request_.attributes().request().http().headers().contains("not-allowed-or-disallowed"));
}

// Verify that check request object has only a portion of the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithPartialBody) {
  const uint64_t size = 4049;
  Http::TestRequestHeaderMapImpl headers_;
  envoy::service::auth::v3::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers_, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_, size,
      /*pack_as_bytes=*/false, /*encode_raw_headers=*/false, /*include_peer_certificate=*/false,
      /*include_tls_session=*/false, Protobuf::Map<std::string, std::string>(), nullptr, nullptr);
  ASSERT_EQ(size, request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, size), request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());
  EXPECT_EQ("true", request_.attributes().request().http().headers().at(
                        Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has all the request data.
TEST_F(CheckRequestUtilsTest, BasicHttpWithFullBody) {
  Http::TestRequestHeaderMapImpl headers_;
  envoy::service::auth::v3::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  expectBasicHttp();
  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers_, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_,
      buffer_->length(), /*pack_as_bytes=*/false, /*encode_raw_headers=*/false,
      /*include_peer_certificate=*/false, /*include_tls_session=*/false,
      Protobuf::Map<std::string, std::string>(), nullptr, nullptr);
  ASSERT_EQ(buffer_->length(), request_.attributes().request().http().body().size());
  EXPECT_EQ(buffer_->toString().substr(0, buffer_->length()),
            request_.attributes().request().http().body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());
  EXPECT_EQ("false", request_.attributes().request().http().headers().at(
                         Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has all the request data and packed as bytes instead of UTF-8
// string.
TEST_F(CheckRequestUtilsTest, BasicHttpWithFullBodyPackAsBytes) {
  Http::TestRequestHeaderMapImpl headers_;
  envoy::service::auth::v3::CheckRequest request_;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  // Fill the buffer with non UTF-8 data.
  uint8_t raw[2] = {0xc0, 0xc0};
  Buffer::OwnedImpl raw_buffer(raw, 2);
  buffer_->drain(buffer_->length());
  buffer_->add(raw_buffer);

  expectBasicHttp();

  // Setting pack_as_bytes as false and a string field with invalid UTF-8 data makes
  // calling request_.SerializeToString() below to print an error message to stderr. Interestingly,
  // request_.SerializeToString() still returns "true" when it is failed to serialize the data.
  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers_, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request_,
      buffer_->length(), /*pack_as_bytes=*/true, /*encode_raw_headers=*/false,
      /*include_peer_certificate=*/false, /*include_tls_session=*/false,
      Protobuf::Map<std::string, std::string>(), nullptr, nullptr);

  // TODO(dio): Find a way to test this without using function from testing::internal namespace.
  testing::internal::CaptureStderr();
  std::string out;
  ASSERT_TRUE(request_.SerializeToString(&out));
  ASSERT_EQ("", testing::internal::GetCapturedStderr());

  // Non UTF-8 data sets raw_body field, instead of body field.
  ASSERT_EQ(buffer_->length(), request_.attributes().request().http().raw_body().size());
  ASSERT_EQ(0, request_.attributes().request().http().body().size());

  EXPECT_EQ(buffer_->toString().substr(0, buffer_->length()),
            request_.attributes().request().http().raw_body());
  EXPECT_FALSE(request_.attributes().request().http().has_header_map());
  EXPECT_EQ("false", request_.attributes().request().http().headers().at(
                         Headers::get().EnvoyAuthPartialBody.get()));
}

// Verify that check request object has all the request headers as bytes instead of UTF-8 strings.
TEST_F(CheckRequestUtilsTest, BasicHttpWithHeadersAsBytes) {
  Http::TestRequestHeaderMapImpl headers;
  // Add a header with non-UTF-8 data in its value.
  absl::string_view header_key = "header-with-non-utf-8-value";
  const uint8_t non_utf_8_bytes[3] = {0xc0, 0xc0, 0};
  absl::string_view header_value = reinterpret_cast<const char*>(non_utf_8_bytes);
  headers.addCopy(Http::LowerCaseString(header_key), header_value);

  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  expectBasicHttp();

  // Setting encode_raw_headers to true
  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request,
      buffer_->length(), /*pack_as_bytes=*/false, /*encode_raw_headers=*/true,
      /*include_peer_certificate=*/false, /*include_tls_session=*/false,
      Protobuf::Map<std::string, std::string>(), nullptr, nullptr);

  // Headers field should be empty since ext_authz should populate header_map INSTEAD.
  EXPECT_EQ(0, request.attributes().request().http().headers().size());
  ASSERT_TRUE(request.attributes().request().http().has_header_map());

  expectHeadersInHeaderMap(
      request.attributes().request().http().header_map(),
      {{header_key, header_value}, {Headers::get().EnvoyAuthPartialBody.get(), "false"}});
}

// Test that headers with the same key are not concatenated in header_map.
TEST_F(CheckRequestUtilsTest, HeadersAsBytesNoConcatentation) {
  Http::TestRequestHeaderMapImpl headers;
  // Add two headers with the same key.
  absl::string_view header_key = "foo-key";
  absl::string_view header_value_1 = "foo value 1";
  absl::string_view header_value_2 = "foo value 2";
  headers.addCopy(Http::LowerCaseString(header_key), header_value_1);
  headers.addCopy(Http::LowerCaseString(header_key), header_value_2);

  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  expectBasicHttp();

  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request,
      buffer_->length(), /*pack_as_bytes=*/false, /*encode_raw_headers=*/true,
      /*include_peer_certificate=*/false, /*include_tls_session=*/false,
      Protobuf::Map<std::string, std::string>(), nullptr, nullptr);

  EXPECT_EQ(0, request.attributes().request().http().headers().size());
  ASSERT_TRUE(request.attributes().request().http().has_header_map());

  // Check headers were not concatenated.
  expectHeadersInHeaderMap(request.attributes().request().http().header_map(),
                           {{header_key, header_value_1}, {header_key, header_value_2}});
}

// If the request already contains the partial body header, it is not included in the authz request.
TEST_F(CheckRequestUtilsTest, HeadersAsBytesExistingPartialBodyHeader) {
  Http::TestRequestHeaderMapImpl headers;
  // Add a partial body header (as if it were defined in the downstream request for some reason).
  absl::string_view header_key = Headers::get().EnvoyAuthPartialBody.get();
  absl::string_view partial_body_preexisting_value = "downstream partial body header value";
  headers.addCopy(Http::LowerCaseString(header_key), partial_body_preexisting_value);

  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  expectBasicHttp();

  CheckRequestUtils::createHttpCheck(
      &callbacks_, headers, Protobuf::Map<std::string, std::string>(),
      envoy::config::core::v3::Metadata(), envoy::config::core::v3::Metadata(), request,
      buffer_->length(), /*pack_as_bytes=*/false, /*encode_raw_headers=*/true,
      /*include_peer_certificate=*/false, /*include_tls_session=*/false,
      Protobuf::Map<std::string, std::string>(), nullptr, nullptr);

  EXPECT_EQ(0, request.attributes().request().http().headers().size());
  ASSERT_TRUE(request.attributes().request().http().has_header_map());

  // Check preexisting partial body header was not included in the authz request.
  expectHeadersNotInHeaderMap(request.attributes().request().http().header_map(),
                              {{header_key, partial_body_preexisting_value}});
}

// Verify that createHttpCheck extract the proper attributes from the http request into CheckRequest
// proto object.
// Verify that the source certificate is not set by default.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeer) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-downstream-service-cluster", "foo"},
                                                 {":path", "/bar"}};
  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(callbacks_, connection())
      .Times(2)
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(Const(connection_), ssl()).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(callbacks_, streamId()).WillRepeatedly(Return(0));
  EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(callbacks_, decodingBuffer());
  EXPECT_CALL(req_info_, protocol()).WillRepeatedly(ReturnPointee(&protocol_));
  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  callHttpCheckAndValidateRequestAttributes(false, nullptr);
}

// Verify that createHttpCheck extract the attributes from the HTTP request into CheckRequest
// proto object and URI SAN is used as principal if present.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerUriSans) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));

  callHttpCheckAndValidateRequestAttributes(false, nullptr);
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

  callHttpCheckAndValidateRequestAttributes(false, nullptr);
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

  callHttpCheckAndValidateRequestAttributes(false, nullptr);
}

// Verify that the source certificate is populated correctly.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerCertificate) {
  expectBasicHttp();

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  EXPECT_CALL(*ssl_, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(cert_data_));

  callHttpCheckAndValidateRequestAttributes(true, nullptr);
}

// Verify that the SNI is populated correctly.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerTLSSession) {
  EXPECT_CALL(callbacks_, connection())
      .Times(3)
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(Const(connection_), ssl()).Times(3).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(callbacks_, streamId()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, decodingBuffer()).WillOnce(Return(buffer_.get()));
  EXPECT_CALL(callbacks_, streamInfo()).WillOnce(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, protocol()).Times(2).WillRepeatedly(ReturnPointee(&protocol_));
  EXPECT_CALL(req_info_, startTime()).WillOnce(Return(SystemTime()));

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  envoy::service::auth::v3::AttributeContext_TLSSession want_tls_session;
  want_tls_session.set_sni(sni_);
  EXPECT_CALL(*ssl_, sni()).Times(2).WillRepeatedly(ReturnRef(want_tls_session.sni()));

  callHttpCheckAndValidateRequestAttributes(false, &want_tls_session);
}

// Verify that the SNI is populated correctly.
TEST_F(CheckRequestUtilsTest, CheckAttrContextPeerTLSSessionWithoutSNI) {
  EXPECT_CALL(callbacks_, connection())
      .Times(3)
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(Const(connection_), ssl()).Times(3).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(connection_, requestedServerName())
      .Times(2)
      .WillRepeatedly(Return(requested_server_name_));
  EXPECT_CALL(callbacks_, streamId()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, decodingBuffer()).WillOnce(Return(buffer_.get()));
  EXPECT_CALL(callbacks_, streamInfo()).WillOnce(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, protocol()).Times(2).WillRepeatedly(ReturnPointee(&protocol_));
  EXPECT_CALL(req_info_, startTime()).WillOnce(Return(SystemTime()));

  EXPECT_CALL(*ssl_, uriSanPeerCertificate()).WillOnce(Return(std::vector<std::string>{"source"}));
  EXPECT_CALL(*ssl_, uriSanLocalCertificate())
      .WillOnce(Return(std::vector<std::string>{"destination"}));
  envoy::service::auth::v3::AttributeContext_TLSSession want_tls_session;
  EXPECT_CALL(*ssl_, sni()).WillOnce(ReturnRef(want_tls_session.sni()));

  callHttpCheckAndValidateRequestAttributes(false, &want_tls_session);
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
