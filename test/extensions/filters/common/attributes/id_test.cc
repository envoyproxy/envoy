#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/common/attributes/id.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {
namespace {
static inline void verifyPath(absl::string_view path, RootToken root, SubToken sub) {
  auto attr = AttributeId::from_path(path);
  EXPECT_TRUE(attr.has_value());
  EXPECT_EQ(attr->root(), root);

  auto sub_tok = attr->sub();
  EXPECT_TRUE(sub_tok.has_value());
  EXPECT_EQ(sub_tok.value(), sub);
}

TEST(AttributeId, EmptyPath) {
  auto attr = AttributeId::from_path("");
  EXPECT_FALSE(attr.has_value());
}
TEST(AttributeId, UnknownTopLevel) {
  {
    auto attr = AttributeId::from_path("foobar");
    EXPECT_FALSE(attr.has_value());
  }
  {
    auto attr = AttributeId::from_path("baz");
    EXPECT_FALSE(attr.has_value());
  }
}
TEST(AttributeId, TopLevelDots) {
  {
    auto attr = AttributeId::from_path("foo.");
    EXPECT_FALSE(attr.has_value());
  }
  {
    auto attr = AttributeId::from_path(".baz");
    EXPECT_FALSE(attr.has_value());
  }
}
// TEST(AttributeId, ValidTopLevels) {
//   {
//     auto attr = AttributeId::from_path("request");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("response");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("source");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("destination");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("connection");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("upstream");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("metadata");
//     EXPECT_FALSE(attr.has_value());
//   }
//   {
//     auto attr = AttributeId::from_path("filter_state");
//     EXPECT_FALSE(attr.has_value());
//   }
// }
TEST(AttributeId, RequestAttributes) {
  verifyPath("request.path", RootToken::REQUEST, SubToken(RequestToken::PATH));
  verifyPath("request.url_path", RootToken::REQUEST, SubToken(RequestToken::URL_PATH));
  verifyPath("request.host", RootToken::REQUEST, SubToken(RequestToken::HOST));
  verifyPath("request.scheme", RootToken::REQUEST, SubToken(RequestToken::SCHEME));
  verifyPath("request.method", RootToken::REQUEST, SubToken(RequestToken::METHOD));
  verifyPath("request.headers", RootToken::REQUEST, SubToken(RequestToken::HEADERS));
  verifyPath("request.referer", RootToken::REQUEST, SubToken(RequestToken::REFERER));
  verifyPath("request.useragent", RootToken::REQUEST, SubToken(RequestToken::USERAGENT));
  verifyPath("request.time", RootToken::REQUEST, SubToken(RequestToken::TIME));
  verifyPath("request.id", RootToken::REQUEST, SubToken(RequestToken::ID));
  verifyPath("request.duration", RootToken::REQUEST, SubToken(RequestToken::DURATION));
  verifyPath("request.size", RootToken::REQUEST, SubToken(RequestToken::SIZE));
  verifyPath("request.total_size", RootToken::REQUEST, SubToken(RequestToken::TOTAL_SIZE));

  verifyPath("response.code", RootToken::RESPONSE, SubToken(ResponseToken::CODE));
  verifyPath("response.code_details", RootToken::RESPONSE, SubToken(ResponseToken::CODE_DETAILS));
  verifyPath("response.flags", RootToken::RESPONSE, SubToken(ResponseToken::FLAGS));
  verifyPath("response.grpc_status", RootToken::RESPONSE, SubToken(ResponseToken::GRPC_STATUS));
  verifyPath("response.headers", RootToken::RESPONSE, SubToken(ResponseToken::HEADERS));
  verifyPath("response.trailers", RootToken::RESPONSE, SubToken(ResponseToken::TRAILERS));
  verifyPath("response.size", RootToken::RESPONSE, SubToken(ResponseToken::SIZE));
  verifyPath("response.total_size", RootToken::RESPONSE, SubToken(ResponseToken::TOTAL_SIZE));

  verifyPath("source.address", RootToken::SOURCE, SubToken(SourceToken::ADDRESS));
  verifyPath("source.port", RootToken::SOURCE, SubToken(SourceToken::PORT));

  verifyPath("destination.address", RootToken::DESTINATION, SubToken(DestinationToken::ADDRESS));
  verifyPath("destination.port", RootToken::DESTINATION, SubToken(DestinationToken::PORT));

  verifyPath("connection.id", RootToken::CONNECTION, SubToken(ConnectionToken::ID));
  verifyPath("connection.mtls", RootToken::CONNECTION, SubToken(ConnectionToken::MTLS));
  verifyPath("connection.requested_server_name", RootToken::CONNECTION,
             SubToken(ConnectionToken::REQUESTED_SERVER_NAME));
  verifyPath("connection.tls_version", RootToken::CONNECTION,
             SubToken(ConnectionToken::TLS_VERSION));
  verifyPath("connection.subject_local_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::SUBJECT_LOCAL_CERTIFICATE));
  verifyPath("connection.subject_peer_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::SUBJECT_PEER_CERTIFICATE));
  verifyPath("connection.dns_san_local_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::DNS_SAN_LOCAL_CERTIFICATE));
  verifyPath("connection.dns_san_peer_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::DNS_SAN_PEER_CERTIFICATE));
  verifyPath("connection.uri_san_local_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::URI_SAN_LOCAL_CERTIFICATE));
  verifyPath("connection.uri_san_peer_certificate", RootToken::CONNECTION,
             SubToken(ConnectionToken::URI_SAN_PEER_CERTIFICATE));
  verifyPath("connection.termination_details", RootToken::CONNECTION,
             SubToken(ConnectionToken::TERMINATION_DETAILS));

  verifyPath("upstream.address", RootToken::UPSTREAM, SubToken(UpstreamToken::ADDRESS));
  verifyPath("upstream.port", RootToken::UPSTREAM, SubToken(UpstreamToken::PORT));
  verifyPath("upstream.tls_version", RootToken::UPSTREAM, SubToken(UpstreamToken::TLS_VERSION));

  verifyPath("upstream.subject_local_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::SUBJECT_LOCAL_CERTIFICATE));
  verifyPath("upstream.subject_peer_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::SUBJECT_PEER_CERTIFICATE));

  verifyPath("upstream.dns_san_local_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::DNS_SAN_LOCAL_CERTIFICATE));
  verifyPath("upstream.dns_san_peer_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::DNS_SAN_PEER_CERTIFICATE));

  verifyPath("upstream.uri_san_local_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::URI_SAN_LOCAL_CERTIFICATE));
  verifyPath("upstream.uri_san_peer_certificate", RootToken::UPSTREAM,
             SubToken(UpstreamToken::URI_SAN_PEER_CERTIFICATE));

  verifyPath("upstream.local_address", RootToken::UPSTREAM, SubToken(UpstreamToken::LOCAL_ADDRESS));
  verifyPath("upstream.transport_failure_reason", RootToken::UPSTREAM,
             SubToken(UpstreamToken::TRANSPORT_FAILURE_REASON));
}
} // namespace
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy