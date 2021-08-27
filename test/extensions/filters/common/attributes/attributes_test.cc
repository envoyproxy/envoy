#include <chrono>

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/common/attributes/attributes.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Return;
using testing::ReturnNull;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {
namespace {

TEST(Attributes, ValueUtil) {
  google::api::expr::v1alpha1::MapValue* m = nullptr;
  auto succ = ValueUtil::getOrInsert(m, absl::string_view("foobar"));
  EXPECT_FALSE(succ);
}

TEST(Attributes, Private) {
  StreamInfo::MockStreamInfo stream_info;
  auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));

  downstream_addr_provider->setConnectionID(123);

  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(*downstream_addr_provider));
  auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
  auto address_mock = std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");

  EXPECT_CALL(*upstream_host, address()).Times(AtLeast(1)).WillRepeatedly(Return(address_mock));

  EXPECT_CALL(stream_info, upstreamHost()).Times(AtLeast(1)).WillRepeatedly(Return(upstream_host));
  auto upstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(stream_info, upstreamSslConnection())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(upstream_connection_info));

  {
    auto id = AttributeId(RootToken::METADATA, absl::make_optional(RequestToken::PATH));
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(id);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto id = AttributeId(RootToken::FILTER_STATE, absl::make_optional(RequestToken::PATH));
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(id);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<RootToken>(100);
    auto id = AttributeId(tok, absl::make_optional(RequestToken::PATH));
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(id);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<RootToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.full(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<ResponseToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<SourceToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<DestinationToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<UpstreamToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto tok = static_cast<ConnectionToken>(100);
    auto attrs = Attributes(stream_info);
    auto val = attrs.get(tok);
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
  {
    auto attrs = Attributes(stream_info);
    auto val = attrs.getRequestHeaders();
    EXPECT_TRUE(val->has_null_value());
    delete val;
  }
}
TEST(Attributes, DefaultStream) {
  StreamInfo::MockStreamInfo stream_info;
  auto attrs = Attributes(stream_info);
  std::vector<AttributeId> v;
  auto value = attrs.buildAttributesValue(v);

  EXPECT_TRUE(value->has_map_value());
  const auto& map_value = value->map_value();
  EXPECT_EQ(0, map_value.entries_size());
  delete value;
}

TEST(Attributes, SingleAttribute) {
  {
    StreamInfo::MockStreamInfo stream_info;

    auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
        std::make_shared<Network::Address::Ipv4Instance>(80),
        std::make_shared<Network::Address::Ipv4Instance>(80));

    downstream_addr_provider->setConnectionID(123);

    EXPECT_CALL(stream_info, downstreamAddressProvider())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(*downstream_addr_provider));

    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("connection.id").value());

    auto attrs = Attributes(stream_info);
    auto value = attrs.buildAttributesValue(v);
    EXPECT_TRUE(value->has_map_value());
    const auto& map_value = value->map_value();
    EXPECT_EQ(1, map_value.entries_size());

    const auto& entry0 = map_value.entries(0);
    EXPECT_EQ(entry0.key().string_value(), "connection");
    const auto& entry0_value = entry0.value();
    EXPECT_TRUE(entry0_value.has_map_value());
    const auto& entry0_value_map = entry0_value.map_value();
    EXPECT_EQ(1, entry0_value_map.entries_size());

    const auto& entry0_entry0 = entry0_value_map.entries(0);
    EXPECT_TRUE(entry0_entry0.has_key());
    EXPECT_EQ(entry0_entry0.key().string_value(), "id");
    EXPECT_TRUE(entry0_entry0.value().has_uint64_value());
    EXPECT_EQ(entry0_entry0.value().uint64_value(), 123);
    delete value;
  }
}

TEST(Attributes, SingleRootAttr) {
  StreamInfo::MockStreamInfo stream_info;

  // setup downstream connection
  auto downstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();

  auto tls_ver = std::string("tls_ver");
  EXPECT_CALL(*downstream_connection_info, tlsVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(tls_ver));
  EXPECT_CALL(*downstream_connection_info, peerCertificatePresented())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  auto subj_local_cert = std::string("subjlocalcert");
  EXPECT_CALL(*downstream_connection_info, subjectLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(subj_local_cert));

  auto subj_peer_cert = std::string("subjpeercert");
  EXPECT_CALL(*downstream_connection_info, subjectPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(subj_peer_cert));

  std::vector<std::string> dns_local_cert_vec = {"dnslocalcert"};
  EXPECT_CALL(*downstream_connection_info, dnsSansLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(dns_local_cert_vec)));

  std::vector<std::string> dns_peer_cert_vec = {"dnspeercert"};
  EXPECT_CALL(*downstream_connection_info, dnsSansPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(dns_peer_cert_vec)));

  std::vector<std::string> uri_local_cert_vec = {"urilocalcert"};
  EXPECT_CALL(*downstream_connection_info, uriSanLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(uri_local_cert_vec)));

  std::vector<std::string> uri_peer_cert_vec = {"uripeercert"};
  EXPECT_CALL(*downstream_connection_info, uriSanPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(uri_peer_cert_vec)));

  // setup downstream addr provider
  auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));

  downstream_addr_provider->setConnectionID(123);
  downstream_addr_provider->setRequestedServerName("foobar");
  downstream_addr_provider->setSslConnection(downstream_connection_info);
  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(*downstream_addr_provider));

  auto conn_term_details = absl::make_optional(std::string("conn_term_details"));
  EXPECT_CALL(stream_info, connectionTerminationDetails())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(conn_term_details));

  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("connection").value());
  auto attrs = Attributes(stream_info);
  auto value = attrs.buildAttributesValue(v);

  // Verify the output result.
  EXPECT_TRUE(value->has_map_value());
  delete value;
}

TEST(Attributes, StreamAttributes) {
  {
    StreamInfo::MockStreamInfo stream_info;
    EXPECT_CALL(stream_info, protocol())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::make_optional(Http::Protocol::Http2)));

    time_t t_start_time = 1;
    auto start_time = std::chrono::system_clock::from_time_t(t_start_time);
    EXPECT_CALL(stream_info, startTime()).Times(AtLeast(1)).WillRepeatedly(Return(start_time));

    EXPECT_CALL(stream_info, requestComplete())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::make_optional(std::chrono::nanoseconds(11))));

    EXPECT_CALL(stream_info, bytesReceived()).Times(AtLeast(1)).WillRepeatedly(Return(22));
    EXPECT_CALL(stream_info, bytesSent()).Times(AtLeast(1)).WillRepeatedly(Return(33));

    EXPECT_CALL(stream_info, responseFlags()).Times(AtLeast(1)).WillRepeatedly(Return(44));
    EXPECT_CALL(stream_info, responseCode()).Times(AtLeast(1)).WillRepeatedly(Return(411));
    auto conn_term_details = absl::make_optional(std::string("conn_term_details"));
    EXPECT_CALL(stream_info, connectionTerminationDetails())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(conn_term_details));

    auto code_details = absl::make_optional(std::string("response_code_details"));
    EXPECT_CALL(stream_info, responseCodeDetails())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(code_details));

    const Network::Address::InstanceConstSharedPtr upstream_local_addr =
        std::make_shared<const Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");
    EXPECT_CALL(stream_info, setUpstreamLocalAddress(_));
    stream_info.setUpstreamLocalAddress(upstream_local_addr);
    EXPECT_CALL(stream_info, upstreamLocalAddress()).Times(AtLeast(1));

    auto trans_fail_reason = std::string("upstream_transport_failure_reason");
    EXPECT_CALL(stream_info, upstreamTransportFailureReason())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(trans_fail_reason));

    auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
    auto address_mock =
        std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");

    Network::MockIp ip_mock;
    EXPECT_CALL(ip_mock, port()).Times(AtLeast(1)).WillRepeatedly(Return(443));

    EXPECT_CALL(*address_mock, ip()).Times(AtLeast(1)).WillRepeatedly(Return(&ip_mock));
    EXPECT_CALL(*upstream_host, address()).Times(AtLeast(1)).WillRepeatedly(Return(address_mock));

    EXPECT_CALL(stream_info, upstreamHost())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(upstream_host));

    // setup downstream connection
    auto downstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();

    auto tls_ver = std::string("tls_ver");
    EXPECT_CALL(*downstream_connection_info, tlsVersion())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(tls_ver));
    EXPECT_CALL(*downstream_connection_info, peerCertificatePresented())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(true));

    auto subj_local_cert = std::string("subjlocalcert");
    EXPECT_CALL(*downstream_connection_info, subjectLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(subj_local_cert));

    auto subj_peer_cert = std::string("subjpeercert");
    EXPECT_CALL(*downstream_connection_info, subjectPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(subj_peer_cert));

    std::vector<std::string> dns_local_cert_vec = {"dnslocalcert"};
    EXPECT_CALL(*downstream_connection_info, dnsSansLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(dns_local_cert_vec)));

    std::vector<std::string> dns_peer_cert_vec = {"dnspeercert"};
    EXPECT_CALL(*downstream_connection_info, dnsSansPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(dns_peer_cert_vec)));

    std::vector<std::string> uri_local_cert_vec = {"urilocalcert"};
    EXPECT_CALL(*downstream_connection_info, uriSanLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(uri_local_cert_vec)));

    std::vector<std::string> uri_peer_cert_vec = {"uripeercert"};
    EXPECT_CALL(*downstream_connection_info, uriSanPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(uri_peer_cert_vec)));

    // setup downstream addr provider
    auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
        std::make_shared<Network::Address::Ipv4Instance>(80),
        std::make_shared<Network::Address::Ipv4Instance>(80));

    downstream_addr_provider->setConnectionID(123);
    downstream_addr_provider->setRequestedServerName("foobar");
    downstream_addr_provider->setSslConnection(downstream_connection_info);

    EXPECT_CALL(stream_info, downstreamAddressProvider())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(*downstream_addr_provider));

    // setup upstream connection
    auto upstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    auto up_tls_ver = std::string("up_tls_ver");
    EXPECT_CALL(*upstream_connection_info, tlsVersion())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(up_tls_ver));

    auto up_subj_local_cert = std::string("up_subjlocalcert");
    EXPECT_CALL(*upstream_connection_info, subjectLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(up_subj_local_cert));

    auto up_subj_peer_cert = std::string("up_subjpeercert");
    EXPECT_CALL(*upstream_connection_info, subjectPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(up_subj_peer_cert));

    std::vector<std::string> up_dns_local_cert_vec = {"up_dnslocalcert"};
    EXPECT_CALL(*upstream_connection_info, dnsSansLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(up_dns_local_cert_vec)));

    std::vector<std::string> up_dns_peer_cert_vec = {"up_dnspeercert"};
    EXPECT_CALL(*upstream_connection_info, dnsSansPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(up_dns_peer_cert_vec)));

    std::vector<std::string> up_uri_local_cert_vec = {"up_dnslocalcert"};
    EXPECT_CALL(*upstream_connection_info, uriSanLocalCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(up_uri_local_cert_vec)));

    std::vector<std::string> up_uri_peer_cert_vec = {"up_uripeercert"};
    EXPECT_CALL(*upstream_connection_info, uriSanPeerCertificate())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(absl::MakeSpan(up_uri_peer_cert_vec)));

    EXPECT_CALL(stream_info, upstreamSslConnection())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(upstream_connection_info));

    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("connection.id").value());
    v.push_back(AttributeId::fromPath("connection.mtls").value());
    v.push_back(AttributeId::fromPath("connection.requested_server_name").value());

    v.push_back(AttributeId::fromPath("connection.tls_version").value());
    v.push_back(AttributeId::fromPath("connection.subject_local_certificate").value());
    v.push_back(AttributeId::fromPath("connection.subject_peer_certificate").value());
    v.push_back(AttributeId::fromPath("connection.dns_san_local_certificate").value());
    v.push_back(AttributeId::fromPath("connection.dns_san_peer_certificate").value());
    v.push_back(AttributeId::fromPath("connection.uri_san_local_certificate").value());
    v.push_back(AttributeId::fromPath("connection.uri_san_peer_certificate").value());
    v.push_back(AttributeId::fromPath("connection.termination_details").value());

    v.push_back(AttributeId::fromPath("request.time").value());
    v.push_back(AttributeId::fromPath("request.protocol").value());
    v.push_back(AttributeId::fromPath("request.duration").value());
    v.push_back(AttributeId::fromPath("request.size").value());
    v.push_back(AttributeId::fromPath("request.total_size").value());

    v.push_back(AttributeId::fromPath("response.code").value());
    v.push_back(AttributeId::fromPath("response.code_details").value());
    v.push_back(AttributeId::fromPath("response.flags").value());
    v.push_back(AttributeId::fromPath("response.grpc_status").value());
    v.push_back(AttributeId::fromPath("response.headers").value());
    v.push_back(AttributeId::fromPath("response.trailers").value());
    v.push_back(AttributeId::fromPath("response.size").value());
    v.push_back(AttributeId::fromPath("response.total_size").value());

    v.push_back(AttributeId::fromPath("source.address").value());
    v.push_back(AttributeId::fromPath("source.port").value());

    v.push_back(AttributeId::fromPath("destination.address").value());
    v.push_back(AttributeId::fromPath("destination.port").value());

    v.push_back(AttributeId::fromPath("upstream.address").value());
    v.push_back(AttributeId::fromPath("upstream.port").value());
    v.push_back(AttributeId::fromPath("upstream.tls_version").value());
    v.push_back(AttributeId::fromPath("upstream.subject_local_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.subject_peer_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.dns_san_local_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.dns_san_peer_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.uri_san_local_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.uri_san_peer_certificate").value());
    v.push_back(AttributeId::fromPath("upstream.local_address").value());
    v.push_back(AttributeId::fromPath("upstream.transport_failure_reason").value());

    auto attrs = Attributes(stream_info);
    auto value = attrs.buildAttributesValue(v);

    // Verify the output result.
    EXPECT_TRUE(value->has_map_value());
    const auto& map_value = value->map_value();
    EXPECT_EQ(6, map_value.entries_size());

    auto value_string = R"HERE(map_value {
  entries {
    key {
      string_value: "connection"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "id"
          }
          value {
            uint64_value: 123
          }
        }
        entries {
          key {
            string_value: "mtls"
          }
          value {
            bool_value: true
          }
        }
        entries {
          key {
            string_value: "requested_server_name"
          }
          value {
            string_value: "foobar"
          }
        }
        entries {
          key {
            string_value: "tls_version"
          }
          value {
            string_value: "tls_ver"
          }
        }
        entries {
          key {
            string_value: "subject_local_certificate"
          }
          value {
            string_value: "subjlocalcert"
          }
        }
        entries {
          key {
            string_value: "subject_peer_certificate"
          }
          value {
            string_value: "subjpeercert"
          }
        }
        entries {
          key {
            string_value: "dns_san_local_certificate"
          }
          value {
            string_value: "dnslocalcert"
          }
        }
        entries {
          key {
            string_value: "dns_san_peer_certificate"
          }
          value {
            string_value: "dnspeercert"
          }
        }
        entries {
          key {
            string_value: "uri_san_local_certificate"
          }
          value {
            string_value: "urilocalcert"
          }
        }
        entries {
          key {
            string_value: "uri_san_peer_certificate"
          }
          value {
            string_value: "uripeercert"
          }
        }
        entries {
          key {
            string_value: "termination_details"
          }
          value {
            string_value: "conn_term_details"
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "request"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "time"
          }
          value {
            string_value: "1970-01-01T00:00:01Z"
          }
        }
        entries {
          key {
            string_value: "protocol"
          }
          value {
            string_value: "Http 2"
          }
        }
        entries {
          key {
            string_value: "duration"
          }
          value {
            string_value: "11ns"
          }
        }
        entries {
          key {
            string_value: "size"
          }
          value {
            uint64_value: 22
          }
        }
        entries {
          key {
            string_value: "total_size"
          }
          value {
            uint64_value: 22
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "response"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "code"
          }
          value {
            uint64_value: 411
          }
        }
        entries {
          key {
            string_value: "code_details"
          }
          value {
            string_value: "response_code_details"
          }
        }
        entries {
          key {
            string_value: "flags"
          }
          value {
            uint64_value: 44
          }
        }
        entries {
          key {
            string_value: "grpc_status"
          }
          value {
            uint64_value: 2
          }
        }
        entries {
          key {
            string_value: "headers"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "trailers"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "size"
          }
          value {
            uint64_value: 33
          }
        }
        entries {
          key {
            string_value: "total_size"
          }
          value {
            uint64_value: 33
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "source"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            string_value: "1.2.3.4:443"
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            uint64_value: 443
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "destination"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            string_value: "0.0.0.0:80"
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            uint64_value: 80
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "upstream"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            string_value: "1.2.3.4:443"
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            uint64_value: 443
          }
        }
        entries {
          key {
            string_value: "tls_version"
          }
          value {
            string_value: "up_tls_ver"
          }
        }
        entries {
          key {
            string_value: "subject_local_certificate"
          }
          value {
            string_value: "up_subjlocalcert"
          }
        }
        entries {
          key {
            string_value: "subject_peer_certificate"
          }
          value {
            string_value: "up_subjpeercert"
          }
        }
        entries {
          key {
            string_value: "dns_san_local_certificate"
          }
          value {
            string_value: "up_dnslocalcert"
          }
        }
        entries {
          key {
            string_value: "dns_san_peer_certificate"
          }
          value {
            string_value: "up_dnspeercert"
          }
        }
        entries {
          key {
            string_value: "uri_san_local_certificate"
          }
          value {
            string_value: "up_dnslocalcert"
          }
        }
        entries {
          key {
            string_value: "uri_san_peer_certificate"
          }
          value {
            string_value: "up_uripeercert"
          }
        }
        entries {
          key {
            string_value: "local_address"
          }
          value {
            string_value: "1.2.3.4:443"
          }
        }
        entries {
          key {
            string_value: "transport_failure_reason"
          }
          value {
            string_value: "upstream_transport_failure_reason"
          }
        }
      }
    }
  }
}
)HERE";
    EXPECT_EQ(value_string, value->DebugString());
    delete value;
  }
}

TEST(Attributes, ResponseHeadersAndTrailersTest) {
  StreamInfo::MockStreamInfo stream_info;
  envoy::config::core::v3::Metadata meta;

  // create the metadata
  ProtobufWkt::Struct foo_struct;
  ProtobufWkt::Value bar_val;
  bar_val.set_bool_value(true);
  (*foo_struct.mutable_fields())["bar"] = bar_val;
  auto filter_meta = meta.mutable_filter_metadata();
  (*filter_meta)["foo"] = foo_struct;

  EXPECT_CALL(stream_info, dynamicMetadata()).Times(AtLeast(1)).WillRepeatedly(ReturnRef(meta));

  auto attrs = Attributes(stream_info);

  Http::TestResponseHeaderMapImpl response_headers{{"status", "200"},
                                                   {"content-type", "application/json"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"foobar", "2000"}, {"baz", "100"}};

  attrs.setResponseHeaders(&response_headers);
  attrs.setResponseTrailers(&response_trailers);

  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("response.grpc_status").value());

  v.push_back(AttributeId::fromPath("response.headers").value());
  v.push_back(AttributeId::fromPath("response.trailers").value());

  v.push_back(AttributeId::fromPath("metadata").value());
  v.push_back(AttributeId::fromPath("filter_state").value());

  auto value = attrs.buildAttributesValue(v);
  EXPECT_TRUE(value->has_map_value());

  auto expected = R"HERE(map_value {
  entries {
    key {
      string_value: "response"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "grpc_status"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "headers"
          }
          value {
            map_value {
              entries {
                key {
                  string_value: "status"
                }
                value {
                  string_value: "200"
                }
              }
              entries {
                key {
                  string_value: "content-type"
                }
                value {
                  string_value: "application/json"
                }
              }
            }
          }
        }
        entries {
          key {
            string_value: "trailers"
          }
          value {
            map_value {
              entries {
                key {
                  string_value: "foobar"
                }
                value {
                  string_value: "2000"
                }
              }
              entries {
                key {
                  string_value: "baz"
                }
                value {
                  string_value: "100"
                }
              }
            }
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "metadata"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "foo"
          }
          value {
            object_value {
              [type.googleapis.com/google.protobuf.Struct] {
                fields {
                  key: "bar"
                  value {
                    bool_value: true
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "filter_state"
    }
    value {
      null_value: NULL_VALUE
    }
  }
}
)HERE";
  EXPECT_EQ(expected, value->DebugString());
  delete value;
}
TEST(Attributes, EmptyHeaders) {
  StreamInfo::MockStreamInfo stream_info;
  auto attrs = Attributes(stream_info);

  Http::TestRequestHeaderMapImpl request_headers{{"foo", "bar"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"baz", "qux"}};
  attrs.setRequestHeaders(&request_headers);
  attrs.setRequestTrailers(&request_trailers);
  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("request.path").value());
  v.push_back(AttributeId::fromPath("request.url_path").value());

  auto value = attrs.buildAttributesValue(v);
  EXPECT_TRUE(value->has_map_value());
  const auto& req = value->map_value().entries(0);
  EXPECT_TRUE(req.key().string_value() == "request");

  const auto& req_map = req.value().map_value();

  const auto& path_entry = req_map.entries(0);
  EXPECT_TRUE(path_entry.key().string_value() == "path");
  EXPECT_TRUE(path_entry.value().string_value().empty());

  const auto& url_path_entry = req_map.entries(1);
  EXPECT_TRUE(url_path_entry.key().string_value() == "url_path");
  EXPECT_TRUE(url_path_entry.value().string_value().empty());

  delete value;
}

TEST(Attributes, RequestHeaderAttributes) {
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(stream_info, bytesReceived()).Times(AtLeast(1)).WillRepeatedly(Return(22));
  auto attrs = Attributes(stream_info);

  Http::TestRequestHeaderMapImpl request_headers{{"host", "foobar.com"}, {":path", "/request_path"},
                                                 {":scheme", "http"},    {":method", "GET"},
                                                 {"referer", "baz.com"}, {"user-agent", "browser"},
                                                 {"x-request-id", "11"}, {"content-length", "1"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};

  attrs.setRequestHeaders(&request_headers);
  attrs.setRequestTrailers(&request_trailers);

  std::vector<AttributeId> v;

  // semi-required request_headers attributes
  v.push_back(AttributeId::fromPath("request.size").value());
  v.push_back(AttributeId::fromPath("request.total_size").value());
  v.push_back(AttributeId::fromPath("request.path").value());
  v.push_back(AttributeId::fromPath("request.url_path").value());
  v.push_back(AttributeId::fromPath("request.host").value());
  v.push_back(AttributeId::fromPath("request.scheme").value());
  v.push_back(AttributeId::fromPath("request.method").value());
  v.push_back(AttributeId::fromPath("request.referer").value());
  v.push_back(AttributeId::fromPath("request.useragent").value());
  v.push_back(AttributeId::fromPath("request.id").value());
  v.push_back(AttributeId::fromPath("request.headers").value());

  auto value = attrs.buildAttributesValue(v);
  EXPECT_TRUE(value->has_map_value());

  auto expected = R"HERE(map_value {
  entries {
    key {
      string_value: "request"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "size"
          }
          value {
            uint64_value: 1
          }
        }
        entries {
          key {
            string_value: "total_size"
          }
          value {
            uint64_value: 147
          }
        }
        entries {
          key {
            string_value: "path"
          }
          value {
            string_value: "/request_path"
          }
        }
        entries {
          key {
            string_value: "url_path"
          }
          value {
            string_value: "/request_path"
          }
        }
        entries {
          key {
            string_value: "host"
          }
          value {
            string_value: "foobar.com"
          }
        }
        entries {
          key {
            string_value: "scheme"
          }
          value {
            string_value: "http"
          }
        }
        entries {
          key {
            string_value: "method"
          }
          value {
            string_value: "GET"
          }
        }
        entries {
          key {
            string_value: "referer"
          }
          value {
            string_value: "baz.com"
          }
        }
        entries {
          key {
            string_value: "useragent"
          }
          value {
            string_value: "browser"
          }
        }
        entries {
          key {
            string_value: "id"
          }
          value {
            string_value: "11"
          }
        }
        entries {
          key {
            string_value: "headers"
          }
          value {
            map_value {
              entries {
                key {
                  string_value: ":authority"
                }
                value {
                  string_value: "foobar.com"
                }
              }
              entries {
                key {
                  string_value: ":path"
                }
                value {
                  string_value: "/request_path"
                }
              }
              entries {
                key {
                  string_value: ":scheme"
                }
                value {
                  string_value: "http"
                }
              }
              entries {
                key {
                  string_value: ":method"
                }
                value {
                  string_value: "GET"
                }
              }
              entries {
                key {
                  string_value: "referer"
                }
                value {
                  string_value: "baz.com"
                }
              }
              entries {
                key {
                  string_value: "user-agent"
                }
                value {
                  string_value: "browser"
                }
              }
              entries {
                key {
                  string_value: "x-request-id"
                }
                value {
                  string_value: "11"
                }
              }
              entries {
                key {
                  string_value: "content-length"
                }
                value {
                  string_value: "1"
                }
              }
            }
          }
        }
      }
    }
  }
}
)HERE";
  EXPECT_EQ(expected, value->DebugString());
  delete value;
}

TEST(Attributes, AllEmptyAttrs) {
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(stream_info, protocol()).Times(AtLeast(1)).WillRepeatedly(Return(absl::nullopt));

  EXPECT_CALL(stream_info, requestComplete())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::nullopt));

  EXPECT_CALL(stream_info, bytesReceived()).Times(AtLeast(1)).WillRepeatedly(Return(0));
  EXPECT_CALL(stream_info, bytesSent()).Times(AtLeast(1)).WillRepeatedly(Return(0));

  EXPECT_CALL(stream_info, responseFlags()).Times(AtLeast(1)).WillRepeatedly(Return(0));
  EXPECT_CALL(stream_info, responseCode()).Times(AtLeast(1)).WillRepeatedly(Return(absl::nullopt));

  const absl::optional<std::string> term_details = absl::nullopt;
  EXPECT_CALL(stream_info, connectionTerminationDetails())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(term_details));

  const absl::optional<std::string> code_details = absl::nullopt;
  EXPECT_CALL(stream_info, responseCodeDetails())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(code_details));

  const Network::Address::InstanceConstSharedPtr upstream_local_addr(nullptr);
  EXPECT_CALL(stream_info, setUpstreamLocalAddress(_));
  stream_info.setUpstreamLocalAddress(upstream_local_addr);
  EXPECT_CALL(stream_info, upstreamLocalAddress()).Times(AtLeast(1));

  auto trans_fail_reason = std::string("");
  EXPECT_CALL(stream_info, upstreamTransportFailureReason())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(trans_fail_reason));

  std::shared_ptr<Upstream::MockHostDescription> upstream_host;

  EXPECT_CALL(stream_info, upstreamHost()).Times(AtLeast(1)).WillRepeatedly(Return(upstream_host));

  // setup downstream addr provider
  auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));

  downstream_addr_provider->setConnectionID(0);
  downstream_addr_provider->setRequestedServerName("");
  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(*downstream_addr_provider));

  // setup downstream connection
  std::shared_ptr<Ssl::MockConnectionInfo> downstream_connection_info;

  // setup upstream connection
  std::shared_ptr<Ssl::MockConnectionInfo> upstream_connection_info;

  EXPECT_CALL(stream_info, upstreamSslConnection())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(upstream_connection_info));

  auto attrs = Attributes(stream_info);

  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("connection.id").value());
  v.push_back(AttributeId::fromPath("connection.mtls").value());
  v.push_back(AttributeId::fromPath("connection.requested_server_name").value());

  v.push_back(AttributeId::fromPath("connection.tls_version").value());
  v.push_back(AttributeId::fromPath("connection.subject_local_certificate").value());
  v.push_back(AttributeId::fromPath("connection.subject_peer_certificate").value());
  v.push_back(AttributeId::fromPath("connection.dns_san_local_certificate").value());
  v.push_back(AttributeId::fromPath("connection.dns_san_peer_certificate").value());
  v.push_back(AttributeId::fromPath("connection.uri_san_local_certificate").value());
  v.push_back(AttributeId::fromPath("connection.uri_san_peer_certificate").value());
  v.push_back(AttributeId::fromPath("connection.termination_details").value());

  v.push_back(AttributeId::fromPath("request.protocol").value());
  v.push_back(AttributeId::fromPath("request.duration").value());
  v.push_back(AttributeId::fromPath("request.size").value());
  v.push_back(AttributeId::fromPath("request.total_size").value());

  v.push_back(AttributeId::fromPath("response.code").value());
  v.push_back(AttributeId::fromPath("response.code_details").value());
  v.push_back(AttributeId::fromPath("response.flags").value());
  v.push_back(AttributeId::fromPath("response.grpc_status").value());
  v.push_back(AttributeId::fromPath("response.headers").value());
  v.push_back(AttributeId::fromPath("response.trailers").value());
  v.push_back(AttributeId::fromPath("response.size").value());
  v.push_back(AttributeId::fromPath("response.total_size").value());

  v.push_back(AttributeId::fromPath("source.address").value());
  v.push_back(AttributeId::fromPath("source.port").value());

  v.push_back(AttributeId::fromPath("destination.address").value());
  v.push_back(AttributeId::fromPath("destination.port").value());

  v.push_back(AttributeId::fromPath("upstream.address").value());
  v.push_back(AttributeId::fromPath("upstream.port").value());
  v.push_back(AttributeId::fromPath("upstream.tls_version").value());
  v.push_back(AttributeId::fromPath("upstream.subject_local_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.subject_peer_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.dns_san_local_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.dns_san_peer_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.uri_san_local_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.uri_san_peer_certificate").value());
  v.push_back(AttributeId::fromPath("upstream.local_address").value());
  v.push_back(AttributeId::fromPath("upstream.transport_failure_reason").value());

  auto value = attrs.buildAttributesValue(v);
  auto expected = R"HERE(map_value {
  entries {
    key {
      string_value: "connection"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "id"
          }
          value {
            uint64_value: 0
          }
        }
        entries {
          key {
            string_value: "mtls"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "requested_server_name"
          }
          value {
            string_value: ""
          }
        }
        entries {
          key {
            string_value: "tls_version"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "subject_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "subject_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "dns_san_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "dns_san_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "uri_san_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "uri_san_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "termination_details"
          }
          value {
            null_value: NULL_VALUE
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "request"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "protocol"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "duration"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "size"
          }
          value {
            uint64_value: 0
          }
        }
        entries {
          key {
            string_value: "total_size"
          }
          value {
            uint64_value: 0
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "response"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "code"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "code_details"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "flags"
          }
          value {
            uint64_value: 0
          }
        }
        entries {
          key {
            string_value: "grpc_status"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "headers"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "trailers"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "size"
          }
          value {
            uint64_value: 0
          }
        }
        entries {
          key {
            string_value: "total_size"
          }
          value {
            uint64_value: 0
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "source"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            null_value: NULL_VALUE
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "destination"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            string_value: "0.0.0.0:80"
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            uint64_value: 80
          }
        }
      }
    }
  }
  entries {
    key {
      string_value: "upstream"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "address"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "port"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "tls_version"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "subject_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "subject_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "dns_san_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "dns_san_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "uri_san_local_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "uri_san_peer_certificate"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "local_address"
          }
          value {
            null_value: NULL_VALUE
          }
        }
        entries {
          key {
            string_value: "transport_failure_reason"
          }
          value {
            string_value: ""
          }
        }
      }
    }
  }
}
)HERE";
  EXPECT_EQ(expected, value->DebugString());
  delete value;
}

TEST(Attributes, AllRootAttrs) {
  StreamInfo::MockStreamInfo stream_info;

  EXPECT_CALL(stream_info, protocol())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::make_optional(Http::Protocol::Http2)));

  time_t t_start_time = 1;
  auto start_time = std::chrono::system_clock::from_time_t(t_start_time);
  EXPECT_CALL(stream_info, startTime()).Times(AtLeast(1)).WillRepeatedly(Return(start_time));

  EXPECT_CALL(stream_info, requestComplete())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::make_optional(std::chrono::nanoseconds(11))));

  EXPECT_CALL(stream_info, bytesReceived()).Times(AtLeast(1)).WillRepeatedly(Return(22));
  EXPECT_CALL(stream_info, bytesSent()).Times(AtLeast(1)).WillRepeatedly(Return(33));

  EXPECT_CALL(stream_info, responseFlags()).Times(AtLeast(1)).WillRepeatedly(Return(44));
  EXPECT_CALL(stream_info, responseCode()).Times(AtLeast(1)).WillRepeatedly(Return(411));
  auto conn_term_details = absl::make_optional(std::string("conn_term_details"));
  EXPECT_CALL(stream_info, connectionTerminationDetails())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(conn_term_details));

  auto code_details = absl::make_optional(std::string("response_code_details"));
  EXPECT_CALL(stream_info, responseCodeDetails())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(code_details));

  auto upstream_local_addr =
      std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");
  EXPECT_CALL(stream_info, setUpstreamLocalAddress(_));
  stream_info.setUpstreamLocalAddress(upstream_local_addr);
  EXPECT_CALL(stream_info, upstreamLocalAddress()).Times(AtLeast(1));

  auto trans_fail_reason = std::string("upstream_transport_failure_reason");
  EXPECT_CALL(stream_info, upstreamTransportFailureReason())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(trans_fail_reason));

  auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
  auto address_mock = std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");

  Network::MockIp ip_mock;
  EXPECT_CALL(ip_mock, port()).Times(AtLeast(1)).WillRepeatedly(Return(443));

  EXPECT_CALL(*address_mock, ip()).Times(AtLeast(1)).WillRepeatedly(Return(&ip_mock));
  EXPECT_CALL(*upstream_host, address()).Times(AtLeast(1)).WillRepeatedly(Return(address_mock));

  EXPECT_CALL(stream_info, upstreamHost()).Times(AtLeast(1)).WillRepeatedly(Return(upstream_host));

  // setup downstream connection
  auto downstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();

  auto tls_ver = std::string("tls_ver");
  EXPECT_CALL(*downstream_connection_info, tlsVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(tls_ver));
  EXPECT_CALL(*downstream_connection_info, peerCertificatePresented())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  auto subj_local_cert = std::string("subjlocalcert");
  EXPECT_CALL(*downstream_connection_info, subjectLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(subj_local_cert));

  auto subj_peer_cert = std::string("subjpeercert");
  EXPECT_CALL(*downstream_connection_info, subjectPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(subj_peer_cert));

  std::vector<std::string> dns_local_cert_vec = {"dnslocalcert"};
  EXPECT_CALL(*downstream_connection_info, dnsSansLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(dns_local_cert_vec)));

  std::vector<std::string> dns_peer_cert_vec = {"dnspeercert"};
  EXPECT_CALL(*downstream_connection_info, dnsSansPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(dns_peer_cert_vec)));

  std::vector<std::string> uri_local_cert_vec = {"urilocalcert"};
  EXPECT_CALL(*downstream_connection_info, uriSanLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(uri_local_cert_vec)));

  std::vector<std::string> uri_peer_cert_vec = {"uripeercert"};
  EXPECT_CALL(*downstream_connection_info, uriSanPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(uri_peer_cert_vec)));

  // setup downstream addr provider
  auto downstream_addr_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));

  downstream_addr_provider->setConnectionID(123);
  downstream_addr_provider->setRequestedServerName("foobar");
  downstream_addr_provider->setSslConnection(downstream_connection_info);

  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(*downstream_addr_provider));

  // setup upstream connection
  auto upstream_connection_info = std::make_shared<Ssl::MockConnectionInfo>();
  auto up_tls_ver = std::string("up_tls_ver");
  EXPECT_CALL(*upstream_connection_info, tlsVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(up_tls_ver));

  auto up_subj_local_cert = std::string("up_subjlocalcert");
  EXPECT_CALL(*upstream_connection_info, subjectLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(up_subj_local_cert));

  auto up_subj_peer_cert = std::string("up_subjpeercert");
  EXPECT_CALL(*upstream_connection_info, subjectPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(ReturnRef(up_subj_peer_cert));

  std::vector<std::string> up_dns_local_cert_vec = {"up_dnslocalcert"};
  EXPECT_CALL(*upstream_connection_info, dnsSansLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(up_dns_local_cert_vec)));

  std::vector<std::string> up_dns_peer_cert_vec = {"up_dnspeercert"};
  EXPECT_CALL(*upstream_connection_info, dnsSansPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(up_dns_peer_cert_vec)));

  std::vector<std::string> up_uri_local_cert_vec = {"up_dnslocalcert"};
  EXPECT_CALL(*upstream_connection_info, uriSanLocalCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(up_uri_local_cert_vec)));

  std::vector<std::string> up_uri_peer_cert_vec = {"up_uripeercert"};
  EXPECT_CALL(*upstream_connection_info, uriSanPeerCertificate())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(absl::MakeSpan(up_uri_peer_cert_vec)));

  EXPECT_CALL(stream_info, upstreamSslConnection())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(upstream_connection_info));

  auto attrs = Attributes(stream_info);

  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("connection").value());
  v.push_back(AttributeId::fromPath("request").value());
  v.push_back(AttributeId::fromPath("response").value());
  v.push_back(AttributeId::fromPath("source").value());
  v.push_back(AttributeId::fromPath("destination").value());
  v.push_back(AttributeId::fromPath("upstream").value());

  auto value = attrs.buildAttributesValue(v);
  delete value;
}

TEST(Attributes, InvalidContentLength) {
  StreamInfo::MockStreamInfo stream_info;
  auto attrs = Attributes(stream_info);

  Http::TestRequestHeaderMapImpl request_headers{{"content-length", "foobar"}};
  attrs.setRequestHeaders(&request_headers);

  std::vector<AttributeId> v;
  v.push_back(AttributeId::fromPath("request.size").value());
  auto value = attrs.buildAttributesValue(v);

  auto expected = R"HERE(map_value {
  entries {
    key {
      string_value: "request"
    }
    value {
      map_value {
        entries {
          key {
            string_value: "size"
          }
          value {
            null_value: NULL_VALUE
          }
        }
      }
    }
  }
}
)HERE";
  EXPECT_EQ(expected, value->DebugString());
  delete value;
}
TEST(Attributes, NullSourceParts) {
  {
    // source: no host
    StreamInfo::MockStreamInfo stream_info;
    EXPECT_CALL(stream_info, upstreamHost()).Times(AtLeast(0)).WillRepeatedly(ReturnNull());

    auto attrs = Attributes(stream_info);
    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("source.address").value());
    v.push_back(AttributeId::fromPath("source.port").value());

    auto value = attrs.buildAttributesValue(v);

    EXPECT_TRUE(value->has_map_value());
    auto req = value->map_value().entries(0);
    EXPECT_TRUE(req.key().string_value() == "source");

    auto req_map = req.value().map_value();

    const auto& path_entry = req_map.entries(0);
    EXPECT_TRUE(path_entry.key().string_value() == "address");
    EXPECT_TRUE(path_entry.value().has_null_value());

    const auto& url_path_entry = req_map.entries(1);
    EXPECT_TRUE(url_path_entry.key().string_value() == "port");
    EXPECT_TRUE(url_path_entry.value().has_null_value());
    delete value;
  }
  {
    // source: no address
    StreamInfo::MockStreamInfo stream_info;
    auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
    EXPECT_CALL(*upstream_host, address()).Times(AtLeast(1)).WillRepeatedly(ReturnNull());
    EXPECT_CALL(stream_info, upstreamHost())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(upstream_host));

    auto attrs = Attributes(stream_info);
    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("source.address").value());
    v.push_back(AttributeId::fromPath("source.port").value());

    auto value = attrs.buildAttributesValue(v);

    EXPECT_TRUE(value->has_map_value());
    auto req = value->map_value().entries(0);
    EXPECT_TRUE(req.key().string_value() == "source");

    auto req_map = req.value().map_value();

    const auto& path_entry = req_map.entries(0);
    EXPECT_TRUE(path_entry.key().string_value() == "address");
    EXPECT_TRUE(path_entry.value().has_null_value());

    const auto& url_path_entry = req_map.entries(1);
    EXPECT_TRUE(url_path_entry.key().string_value() == "port");
    EXPECT_TRUE(url_path_entry.value().has_null_value());
    delete value;
  }
  {
    // no port
    StreamInfo::MockStreamInfo stream_info;
    auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
    auto address_mock =
        std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");

    EXPECT_CALL(*address_mock, ip()).Times(AtLeast(1)).WillRepeatedly(ReturnNull());
    EXPECT_CALL(*upstream_host, address()).Times(AtLeast(1)).WillRepeatedly(Return(address_mock));

    EXPECT_CALL(stream_info, upstreamHost())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(upstream_host));

    auto attrs = Attributes(stream_info);
    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("source.address").value());
    v.push_back(AttributeId::fromPath("source.port").value());

    auto value = attrs.buildAttributesValue(v);

    EXPECT_TRUE(value->has_map_value());
    auto req = value->map_value().entries(0);
    EXPECT_TRUE(req.key().string_value() == "source");

    auto req_map = req.value().map_value();

    const auto& path_entry = req_map.entries(0);
    EXPECT_TRUE(path_entry.key().string_value() == "address");
    EXPECT_TRUE(path_entry.value().string_value() == "1.2.3.4:443");

    const auto& url_path_entry = req_map.entries(1);
    EXPECT_TRUE(url_path_entry.key().string_value() == "port");
    EXPECT_TRUE(url_path_entry.value().has_null_value());
    delete value;
  }
}

TEST(Attributes, NullDestParts) {
  {
    // destination: no address
    StreamInfo::MockStreamInfo stream_info;
    auto downstream_addr_provider =
        std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
    EXPECT_CALL(stream_info, downstreamAddressProvider())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(*downstream_addr_provider));

    auto attrs = Attributes(stream_info);
    std::vector<AttributeId> v;
    v.push_back(AttributeId::fromPath("destination.address").value());
    v.push_back(AttributeId::fromPath("destination.port").value());

    auto value = attrs.buildAttributesValue(v);

    EXPECT_TRUE(value->has_map_value());
    auto req = value->map_value().entries(0);
    EXPECT_TRUE(req.key().string_value() == "destination");

    auto req_map = req.value().map_value();

    const auto& path_entry = req_map.entries(0);
    EXPECT_TRUE(path_entry.key().string_value() == "address");
    EXPECT_TRUE(path_entry.value().has_null_value());

    const auto& url_path_entry = req_map.entries(1);
    EXPECT_TRUE(url_path_entry.key().string_value() == "port");
    EXPECT_TRUE(url_path_entry.value().has_null_value());

    delete value;
  }
}

} // namespace
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
