#include <ares.h>

#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/dns.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/network/dns_resolver/cares/dns_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
#include "absl/container/node_hash_map.h"
#include "ares.h"
#include "ares_dns.h"
#include "gtest/gtest.h"

#if !defined(WIN32)
#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>
#else
#include "ares_nameser.h"
#endif

using testing::_;
using testing::Contains;
using testing::InSequence;
using testing::IsSupersetOf;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::UnorderedElementsAreArray;

namespace Envoy {
namespace Network {
namespace {

// List of IP address (in human readable format).
using IpList = std::list<std::string>;
// Map from hostname to IpList.
using HostMap = absl::node_hash_map<std::string, IpList>;
// Map from hostname to CNAME
using CNameMap = absl::node_hash_map<std::string, std::string>;

class TestDnsServerQuery {
public:
  TestDnsServerQuery(ConnectionPtr connection, const HostMap& hosts_a, const HostMap& hosts_aaaa,
                     const CNameMap& cnames, const std::chrono::seconds& record_ttl,
                     const std::chrono::seconds& cname_ttl_, bool refused, bool error_on_a,
                     bool error_on_aaaa, bool no_response)
      : connection_(std::move(connection)), hosts_a_(hosts_a), hosts_aaaa_(hosts_aaaa),
        cnames_(cnames), record_ttl_(record_ttl), cname_ttl_(cname_ttl_), refused_(refused),
        error_on_a_(error_on_a), error_on_aaaa_(error_on_aaaa), no_response_(no_response) {
    connection_->addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
  }

  ~TestDnsServerQuery() { connection_->close(ConnectionCloseType::NoFlush); }

  // Utility to encode a dns string in the rfc format. Example: \004some\004good\006domain
  // RFC link: https://www.ietf.org/rfc/rfc1035.txt
  static std::string encodeDnsName(const std::string& input) {
    auto name_split = StringUtil::splitToken(input, ".");
    std::string res;
    for (const auto& it : name_split) {
      res += static_cast<char>(it.size());
      const std::string part{it};
      res.append(part);
    }
    return res;
  }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(TestDnsServerQuery& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      onDataInternal(data);
      return Network::FilterStatus::StopIteration;
    }

    // Hack: void returning variation of onData to allow gtest assertions.
    void onDataInternal(Buffer::Instance& data) {
      buffer_.add(data);
      while (true) {
        if (size_ == 0) {
          uint16_t size_n;
          if (buffer_.length() < sizeof(size_n)) {
            // If we don't have enough bytes to determine size, wait until we do.
            return;
          }
          void* mem = buffer_.linearize(sizeof(size_n));
          std::memcpy(reinterpret_cast<void*>(&size_n), mem, sizeof(size_n));
          buffer_.drain(sizeof(size_n));
          size_ = ntohs(size_n);
        }

        if (buffer_.length() < size_) {
          // If we don't have enough bytes to read the complete query, wait until
          // we do.
          return;
        }

        // Expect requests to be small, so stack allocation is fine for test code.
        unsigned char* request = static_cast<unsigned char*>(buffer_.linearize(size_));
        // Only expecting a single question.
        ASSERT_EQ(1, DNS_HEADER_QDCOUNT(request));
        // Decode the question and perform lookup.
        const unsigned char* question = request + HFIXEDSZ;
        // The number of bytes the encoded question name takes up in the request.
        // Useful in the response when generating resource records containing the
        // name.
        long name_len;
        // Get host name from query and use the name to lookup a record
        // in a host map. If the query type is of type A, then perform the lookup in
        // the hosts_a_ host map. If the query type is of type AAAA, then perform the
        // lookup in the `hosts_aaaa_` host map.
        char* name;
        ASSERT_EQ(ARES_SUCCESS, ares_expand_name(question, request, size_, &name, &name_len));
        // We only expect resources of type A or AAAA.
        const int q_type = DNS_QUESTION_TYPE(question + name_len);

        auto lookup_name = std::string(name);

        auto encoded_name = TestDnsServerQuery::encodeDnsName(name);
        std::string encoded_cname;
        const auto cname = lookupCname(lookup_name);

        if (!cname.empty()) {
          ASSERT_TRUE(cname.size() <= 253);
          lookup_name = const_cast<char*>(cname.c_str());
          encoded_cname = TestDnsServerQuery::encodeDnsName(cname);
        }

        ares_free_string(name);

        ASSERT_TRUE(q_type == T_A || q_type == T_AAAA);
        if (q_type == T_A || q_type == T_AAAA) {
          const auto addrs = getAddrs(q_type, lookup_name);
          auto buf = createAddrResolutionBuffer(q_type, addrs, request, name_len, encoded_cname,
                                                encoded_name);
          if (!parent_.no_response_) {
            parent_.connection_->write(buf, false);
          }

          // Reset query state, time for the next one.
          buffer_.drain(size_);
          size_ = 0;
        }
      }
    }

    TestDnsServerQuery& parent_;
    // The expected size of the current DNS query to read. If zero, indicates that
    // no DNS query is in progress and that a 2 byte size is expected from the
    // client to indicate the next DNS query size.
    uint16_t size_ = 0;
    Buffer::OwnedImpl buffer_;

    std::string lookupCname(const std::string& name) {
      std::string cname;
      // check if we have a cname. If so, we will need to send a response element with the cname
      // and lookup the ips of the cname and send back those ips (if any) too
      auto cit = parent_.cnames_.find(name);
      if (cit != parent_.cnames_.end()) {
        cname = cit->second;
      }

      return cname;
    }

    const std::list<std::string> getAddrs(const int q_type, const std::string& name) {
      std::list<std::string> ips;
      if (q_type == T_A) {
        auto it = parent_.hosts_a_.find(name);
        if (it != parent_.hosts_a_.end()) {
          ips = it->second;
        }
      } else if (q_type == T_AAAA) {
        auto it = parent_.hosts_aaaa_.find(name);
        if (it != parent_.hosts_aaaa_.end()) {
          ips = it->second;
        }
      }
      return ips;
    }

    size_t getAnswersLen(int q_type, const std::list<std::string>& addrs, int query_name_len,
                         const std::string& cname) {
      size_t len = 0;

      if (!cname.empty()) {
        len += query_name_len + RRFIXEDSZ + cname.length() + 1;
        query_name_len = cname.length() + 1;
      }

      if (q_type == T_A) {
        len += addrs.size() * (query_name_len + RRFIXEDSZ + sizeof(in_addr));
      } else if (q_type == T_AAAA) {
        len += addrs.size() * (query_name_len + RRFIXEDSZ + sizeof(in6_addr));
      }

      return len;
    }

    void writeHeaderAndQuestion(Buffer::OwnedImpl& buf, const int q_type, uint16_t answer_count,
                                uint16_t answer_byte_len, size_t qfield_size,
                                unsigned char* request) {
      const size_t response_base_len = HFIXEDSZ + qfield_size;
      absl::FixedArray<unsigned char> response_buf(response_base_len);
      unsigned char* response_base = response_buf.begin();
      memcpy(response_base, request, response_base_len);
      DNS_HEADER_SET_QR(response_base, 1);
      DNS_HEADER_SET_AA(response_base, 0);
      if (parent_.refused_) {
        DNS_HEADER_SET_RCODE(response_base, REFUSED);
      } else if (q_type == T_A && parent_.error_on_a_) {
        // Use `FORMERR` here as a most of the error codes (`SERVFAIL`, `NOTIMP`, `REFUSED`) result
        // in a dirty channel. See `DnsImplTest::DestroyChannelOnRefused` for details.
        DNS_HEADER_SET_RCODE(response_base, FORMERR);
      } else if (q_type == T_AAAA && parent_.error_on_aaaa_) {
        // Use `FORMERR` here as a most of the error codes (`SERVFAIL`, `NOTIMP`, `REFUSED`) result
        // in a dirty channel. See `DnsImplTest::DestroyChannelOnRefused` for details.
        DNS_HEADER_SET_RCODE(response_base, FORMERR);
      } else {
        DNS_HEADER_SET_RCODE(response_base, answer_count > 0 ? NOERROR : NXDOMAIN);
      }
      DNS_HEADER_SET_ANCOUNT(response_base, answer_count);
      DNS_HEADER_SET_NSCOUNT(response_base, 0);
      DNS_HEADER_SET_ARCOUNT(response_base, 0);
      const uint16_t response_size_n = htons(response_base_len + answer_byte_len);

      // Write response header
      buf.add(&response_size_n, sizeof(response_size_n));
      buf.add(response_base, response_base_len);
    }

    void writeAddrRecord(Buffer::OwnedImpl& buf, const std::list<std::string>& ips, int q_type,
                         const std::string& name) {
      // Create a resource record for each IP found in the host map.
      unsigned char response_rr_fixed[RRFIXEDSZ];
      if (q_type == T_A) {
        DNS_RR_SET_TYPE(response_rr_fixed, T_A);
        DNS_RR_SET_LEN(response_rr_fixed, sizeof(in_addr));
      } else {
        DNS_RR_SET_TYPE(response_rr_fixed, T_AAAA);
        DNS_RR_SET_LEN(response_rr_fixed, sizeof(in6_addr));
      }
      DNS_RR_SET_CLASS(response_rr_fixed, C_IN);
      DNS_RR_SET_TTL(response_rr_fixed, parent_.record_ttl_.count());

      for (const auto& it : ips) {
        buf.add(name.c_str(), name.size() + 1);
        buf.add(response_rr_fixed, RRFIXEDSZ);
        if (q_type == T_A) {
          in_addr addr;
          ASSERT_EQ(1, inet_pton(AF_INET, it.c_str(), &addr));
          buf.add(&addr, sizeof(addr));
        } else {
          in6_addr addr;
          ASSERT_EQ(1, inet_pton(AF_INET6, it.c_str(), &addr));
          buf.add(&addr, sizeof(addr));
        }
      }
    }

    void writeCnameRecord(Buffer::OwnedImpl& buf, const std::string& cname,
                          const std::string& encoded_name) {
      unsigned char cname_rr_fixed[RRFIXEDSZ];
      DNS_RR_SET_TYPE(cname_rr_fixed, T_CNAME);
      DNS_RR_SET_LEN(cname_rr_fixed, cname.size() + 1);
      DNS_RR_SET_CLASS(cname_rr_fixed, C_IN);
      DNS_RR_SET_TTL(cname_rr_fixed, parent_.cname_ttl_.count());
      buf.add(encoded_name.c_str(), encoded_name.size() + 1);
      buf.add(cname_rr_fixed, RRFIXEDSZ);
      buf.add(cname.c_str(), cname.size() + 1);
    }

    Buffer::OwnedImpl createAddrResolutionBuffer(const int q_type, const std::list<std::string> ips,
                                                 unsigned char* request, long name_len,
                                                 const std::string& encoded_cname,
                                                 const std::string& encoded_name) {
      Buffer::OwnedImpl write_buffer;
      const size_t qfield_size = name_len + QFIXEDSZ;
      const size_t answer_byte_len = getAnswersLen(q_type, ips, name_len, encoded_cname);
      int answer_count = ips.size();
      answer_count += !encoded_cname.empty() ? 1 : 0;

      // Write response header
      writeHeaderAndQuestion(write_buffer, q_type, answer_count, answer_byte_len, qfield_size,
                             request);

      // if we have a cname, create a resource record
      if (!encoded_cname.empty()) {
        writeCnameRecord(write_buffer, encoded_cname, encoded_name);
        writeAddrRecord(write_buffer, ips, q_type, encoded_cname);
      } else {
        writeAddrRecord(write_buffer, ips, q_type, encoded_name);
      }

      return write_buffer;
    }
  };

private:
  ConnectionPtr connection_;
  const HostMap& hosts_a_;
  const HostMap& hosts_aaaa_;
  const CNameMap& cnames_;
  const std::chrono::seconds& record_ttl_;
  const std::chrono::seconds& cname_ttl_;
  const bool refused_;
  const bool error_on_a_;
  const bool error_on_aaaa_;
  const bool no_response_{false};
};

class TestDnsServer : public TcpListenerCallbacks {
public:
  TestDnsServer(Event::Dispatcher& dispatcher, bool no_response)
      : dispatcher_(dispatcher), record_ttl_(0), cname_ttl_(0),
        stream_info_(dispatcher.timeSource(), nullptr,
                     StreamInfo::FilterState::LifeSpan::Connection),
        no_response_(no_response) {}

  void onAccept(ConnectionSocketPtr&& socket) override {
    Network::ConnectionPtr new_connection = dispatcher_.createServerConnection(
        std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
    TestDnsServerQuery* query = new TestDnsServerQuery(
        std::move(new_connection), hosts_a_, hosts_aaaa_, cnames_, record_ttl_, cname_ttl_,
        refused_, error_on_a_, error_on_aaaa_, no_response_);
    queries_.emplace_back(query);
  }

  void onReject(RejectCause) override { PANIC("not implemented"); }
  void recordConnectionsAcceptedOnSocketEvent(uint32_t) override {}

  void addHosts(const std::string& hostname, const IpList& ip, const RecordType& type) {
    if (type == RecordType::A) {
      hosts_a_[hostname] = ip;
    } else if (type == RecordType::AAAA) {
      hosts_aaaa_[hostname] = ip;
    }
  }

  void addCName(const std::string& hostname, const std::string& cname) {
    cnames_[hostname] = cname;
  }

  void setRecordTtl(const std::chrono::seconds& ttl) { record_ttl_ = ttl; }
  void setCnameTtl(const std::chrono::seconds& ttl) { cname_ttl_ = ttl; }
  void setRefused(bool refused) { refused_ = refused; }
  void setErrorOnQtypeA(bool error) { error_on_a_ = error; }
  void setErrorOnQtypeAAAA(bool error) { error_on_aaaa_ = error; }

private:
  Event::Dispatcher& dispatcher_;

  HostMap hosts_a_;
  HostMap hosts_aaaa_;
  CNameMap cnames_;
  std::chrono::seconds record_ttl_;
  std::chrono::seconds cname_ttl_;
  bool refused_{};
  bool error_on_a_{};
  bool error_on_aaaa_{};
  // All queries are tracked so we can do resource reclamation when the test is
  // over.
  std::vector<std::unique_ptr<TestDnsServerQuery>> queries_;
  StreamInfo::StreamInfoImpl stream_info_;
  const bool no_response_{false};
};

} // namespace

class DnsResolverImplPeer {
public:
  DnsResolverImplPeer(DnsResolverImpl* resolver) : resolver_(resolver) {}

  ares_channel channel() const { return resolver_->channel_; }
  bool isChannelDirty() const { return resolver_->dirty_channel_; }
  const absl::node_hash_map<int, Event::FileEventPtr>& events() { return resolver_->events_; }
  // Reset the channel state for a DnsResolverImpl such that it will only use
  // TCP and optionally has a zero timeout (for validating timeout behavior).
  void resetChannelTcpOnly(bool zero_timeout) {
    ares_destroy(resolver_->channel_);
    ares_options options;
    // TCP-only connections to TestDnsServer, since even loopback UDP can be
    // lossy with a server under load.
    options.flags = ARES_FLAG_USEVC;
    // Avoid host-specific domain search behavior when testing to improve
    // determinism.
    options.ndomains = 0;
    options.timeout = 0;
    resolver_->initializeChannel(&options, ARES_OPT_FLAGS | ARES_OPT_DOMAINS |
                                               (zero_timeout ? ARES_OPT_TIMEOUTMS : 0));
  }
  bool isCaresDefaultTheOnlyNameserver() { return resolver_->isCaresDefaultTheOnlyNameserver(); }

private:
  DnsResolverImpl* resolver_;
};

class DnsImplConstructor : public testing::Test {
protected:
  DnsImplConstructor()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options_;
};

TEST_F(DnsImplConstructor, SupportsCustomResolvers) {
  char addr4str[INET_ADDRSTRLEN];
  // we pick a port that isn't 53 as the default resolve.conf might be
  // set to point to localhost.
  auto addr4 = Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:54");
  char addr6str[INET6_ADDRSTRLEN];
  auto addr6 = Network::Utility::parseInternetAddressAndPortNoThrow("[::1]:54");

  // convert the address and options into typed_dns_resolver_config
  envoy::config::core::v3::Address dns_resolvers;
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv4Instance(addr4->ip()->addressAsString(), addr4->ip()->port()),
      dns_resolvers);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv6Instance(addr6->ip()->addressAsString(), addr6->ip()->port()),
      dns_resolvers);
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy over dns_resolver_options_
  cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);

  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  Network::DnsResolverFactory& dns_resolver_factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  auto resolver =
      dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
          .value();

  auto peer = std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver.get()));
  ares_addr_port_node* resolvers;
  int result = ares_get_servers_ports(peer->channel(), &resolvers);
  EXPECT_EQ(result, ARES_SUCCESS);
  EXPECT_EQ(resolvers->family, AF_INET);
  EXPECT_EQ(resolvers->udp_port, 54);
  EXPECT_STREQ(inet_ntop(AF_INET, &resolvers->addr.addr4, addr4str, INET_ADDRSTRLEN), "127.0.0.1");
  EXPECT_EQ(resolvers->next->family, AF_INET6);
  EXPECT_EQ(resolvers->next->udp_port, 54);
  EXPECT_STREQ(inet_ntop(AF_INET6, &resolvers->next->addr.addr6, addr6str, INET6_ADDRSTRLEN),
               "::1");
  ares_free_data(resolvers);
}

TEST_F(DnsImplConstructor, SupportsCustomResolversAsFallback) {
  char addr4str[INET_ADDRSTRLEN];
  auto addr4 = Network::Utility::parseInternetAddressNoThrow("1.2.3.4");

  // First, create a resolver with no fallback. Check to see if cares default is
  // the only nameserver.
  bool only_has_default = false;
  {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);

    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
    typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
    Network::DnsResolverFactory& dns_resolver_factory =
        createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
    auto resolver =
        dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
            .value();
    auto peer =
        std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver.get()));
    only_has_default = peer->isCaresDefaultTheOnlyNameserver();
  }

  // Now create a resolver with a failover resolver.
  envoy::config::core::v3::Address dns_resolvers;
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv4Instance(addr4->ip()->addressAsString(), addr4->ip()->port()),
      dns_resolvers);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.set_use_resolvers_as_fallback(true);
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy over dns_resolver_options_
  cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  Network::DnsResolverFactory& dns_resolver_factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  auto resolver =
      dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
          .value();

  auto peer = std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver.get()));
  ares_addr_port_node* resolvers;
  int result = ares_get_servers_ports(peer->channel(), &resolvers);
  EXPECT_EQ(result, ARES_SUCCESS);
  EXPECT_EQ(resolvers->family, AF_INET);
  if (only_has_default) {
    // If cares default was the only resolver, the fallbacks will be used.
    EXPECT_STREQ(inet_ntop(AF_INET, &resolvers->addr.addr4, addr4str, INET_ADDRSTRLEN), "1.2.3.4");
  } else {
    // In the common case, where cares default was not the only resolver, the fallback will not be
    // used.
    EXPECT_STRNE(inet_ntop(AF_INET, &resolvers->addr.addr4, addr4str, INET_ADDRSTRLEN), "1.2.3.4");
  }
  ares_free_data(resolvers);
}

TEST_F(DnsImplConstructor, SupportsMultipleCustomResolversAndDnsOptions) {
  char addr4str[INET_ADDRSTRLEN];
  // we pick a port that isn't 53 as the default resolve.conf might be
  // set to point to localhost.
  auto addr4_a = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:80");
  auto addr4_b = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:81");
  char addr6str[INET6_ADDRSTRLEN];
  auto addr6_a = Network::Utility::parseInternetAddressAndPortNoThrow("[::2]:90");
  auto addr6_b = Network::Utility::parseInternetAddressAndPortNoThrow("[::3]:91");

  // convert the address and options into typed_dns_resolver_config
  envoy::config::core::v3::Address dns_resolvers;
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv4Instance(addr4_a->ip()->addressAsString(), addr4_a->ip()->port()),
      dns_resolvers);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  // copy addr4_a
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy addr4_b
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv4Instance(addr4_b->ip()->addressAsString(), addr4_b->ip()->port()),
      dns_resolvers);
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy addr6_a
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv6Instance(addr6_a->ip()->addressAsString(), addr6_a->ip()->port()),
      dns_resolvers);
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy addr6_b
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv6Instance(addr6_b->ip()->addressAsString(), addr6_b->ip()->port()),
      dns_resolvers);
  cares.add_resolvers()->MergeFrom(dns_resolvers);

  // copy over dns_resolver_options_
  cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);

  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  Network::DnsResolverFactory& dns_resolver_factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  auto resolver =
      dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
          .value();
  auto peer = std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver.get()));
  ares_addr_port_node* resolvers;
  int result = ares_get_servers_ports(peer->channel(), &resolvers);
  EXPECT_EQ(result, ARES_SUCCESS);
  // check v4
  EXPECT_EQ(resolvers->family, AF_INET);
  EXPECT_EQ(resolvers->udp_port, 80);
  EXPECT_STREQ(inet_ntop(AF_INET, &resolvers->addr.addr4, addr4str, INET_ADDRSTRLEN), "1.2.3.4");
  EXPECT_EQ(resolvers->next->family, AF_INET);
  EXPECT_EQ(resolvers->next->udp_port, 81);
  EXPECT_STREQ(inet_ntop(AF_INET, &resolvers->next->addr.addr4, addr4str, INET_ADDRSTRLEN),
               "5.6.7.8");
  // check v6
  EXPECT_EQ(resolvers->next->next->family, AF_INET6);
  EXPECT_EQ(resolvers->next->next->udp_port, 90);
  EXPECT_STREQ(inet_ntop(AF_INET6, &resolvers->next->next->addr.addr6, addr6str, INET6_ADDRSTRLEN),
               "::2");
  EXPECT_EQ(resolvers->next->next->next->family, AF_INET6);
  EXPECT_EQ(resolvers->next->next->next->udp_port, 91);
  EXPECT_STREQ(
      inet_ntop(AF_INET6, &resolvers->next->next->next->addr.addr6, addr6str, INET6_ADDRSTRLEN),
      "::3");

  ares_free_data(resolvers);
}

// Custom instance that dispatches everything to a regular instance except for asString(), where
// it borks the port.
class CustomInstance : public Address::Instance {
public:
  CustomInstance(const std::string& address, uint32_t port) : instance_(address, port) {
    antagonistic_name_ = fmt::format("{}:borked_port_{}", address, port);
  }
  ~CustomInstance() override = default;

  // Address::Instance
  bool operator==(const Address::Instance& rhs) const override {
    return asString() == rhs.asString();
  }
  const std::string& asString() const override { return antagonistic_name_; }
  absl::string_view asStringView() const override { return antagonistic_name_; }
  const std::string& logicalName() const override { return antagonistic_name_; }
  const Address::Ip* ip() const override { return instance_.ip(); }
  const Address::Pipe* pipe() const override { return instance_.pipe(); }
  const Address::EnvoyInternalAddress* envoyInternalAddress() const override {
    return instance_.envoyInternalAddress();
  }
  const sockaddr* sockAddr() const override { return instance_.sockAddr(); }
  socklen_t sockAddrLen() const override { return instance_.sockAddrLen(); }
  absl::string_view addressType() const override { PANIC("not implemented"); }

  Address::Type type() const override { return instance_.type(); }
  const SocketInterface& socketInterface() const override {
    return SocketInterfaceSingleton::get();
  }

private:
  std::string antagonistic_name_;
  Address::Ipv4Instance instance_;
};

TEST_F(DnsImplConstructor, SupportCustomAddressInstances) {
  auto test_instance(std::make_shared<CustomInstance>("127.0.0.1", 45));
  EXPECT_EQ(test_instance->asString(), "127.0.0.1:borked_port_45");

  // Construct a typed_dns_resolver_config based on the IP address and port number.
  envoy::config::core::v3::Address dns_resolvers;
  Network::Utility::addressToProtobufAddress(
      Network::Address::Ipv4Instance(test_instance->ip()->addressAsString(),
                                     test_instance->ip()->port()),
      dns_resolvers);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(dns_resolvers);
  // copy over dns_resolver_options_
  cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);

  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  Network::DnsResolverFactory& dns_resolver_factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  auto resolver =
      dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
          .value();
  auto peer = std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver.get()));
  ares_addr_port_node* resolvers;
  int result = ares_get_servers_ports(peer->channel(), &resolvers);
  EXPECT_EQ(result, ARES_SUCCESS);
  EXPECT_EQ(resolvers->family, AF_INET);
  EXPECT_EQ(resolvers->udp_port, 45);
  char addr4str[INET_ADDRSTRLEN];
  EXPECT_STREQ(inet_ntop(AF_INET, &resolvers->addr.addr4, addr4str, INET_ADDRSTRLEN), "127.0.0.1");
  ares_free_data(resolvers);
}

TEST_F(DnsImplConstructor, BadCustomResolvers) {
  envoy::config::core::v3::Address pipe_address;
  pipe_address.mutable_pipe()->set_path("foo");
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(pipe_address);
  // copy over dns_resolver_options_
  cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options_);

  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  Network::DnsResolverFactory& dns_resolver_factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  EXPECT_EQ(dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
                .status()
                .message(),
            "DNS resolver 'foo' is not an IP address");
}

class DnsImplTest : public testing::TestWithParam<Address::IpVersion> {
public:
  DnsImplTest()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  envoy::config::core::v3::TypedExtensionConfig getTypedDnsResolverConfig(
      const std::vector<Network::Address::InstanceConstSharedPtr>& resolver_inst,
      const envoy::config::core::v3::DnsResolverOptions& dns_resolver_options) {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    auto dns_resolvers = envoy::config::core::v3::Address();

    // Setup the DNS resolver address. Could be either IPv4 or IPv6.
    for (const auto& resolver : resolver_inst) {
      if (resolver->ip() != nullptr) {
        dns_resolvers.mutable_socket_address()->set_address(resolver->ip()->addressAsString());
        dns_resolvers.mutable_socket_address()->set_port_value(resolver->ip()->port());
        cares.add_resolvers()->MergeFrom(dns_resolvers);
      }
    }

    cares.set_filter_unroutable_families(filterUnroutableFamilies());
    cares.set_allocated_udp_max_queries(udpMaxQueries());

    // Copy over the dns_resolver_options_.
    cares.mutable_dns_resolver_options()->MergeFrom(dns_resolver_options);
    // setup the typed config
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
    typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

    return typed_dns_resolver_config;
  }

  void SetUp() override {
    // Instantiate TestDnsServer and listen on a random port on the loopback address.
    server_ = std::make_unique<TestDnsServer>(*dispatcher_, queryTimeout());
    socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(GetParam()));
    NiceMock<Network::MockListenerConfig> listener_config;
    Server::ThreadLocalOverloadStateOptRef overload_state;
    listener_ = std::make_unique<Network::TcpListenerImpl>(
        *dispatcher_, api_->randomGenerator(), runtime_, socket_, *server_,
        listener_config.bindToPort(), listener_config.ignoreGlobalConnLimit(),
        listener_config.shouldBypassOverloadManager(),
        listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
    updateDnsResolverOptions();

    // Create a resolver options on stack here to emulate what actually happens in envoy bootstrap.
    envoy::config::core::v3::DnsResolverOptions dns_resolver_options = dns_resolver_options_;
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    if (setResolverInConstructor()) {
      typed_dns_resolver_config = getTypedDnsResolverConfig(
          {socket_->connectionInfoProvider().localAddress()}, dns_resolver_options);
    } else {
      typed_dns_resolver_config = getTypedDnsResolverConfig({}, dns_resolver_options);
    }
    Network::DnsResolverFactory& dns_resolver_factory =
        createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
    resolver_ =
        dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
            .value();
    // Point c-ares at the listener with no search domains and TCP-only.
    peer_ = std::make_unique<DnsResolverImplPeer>(dynamic_cast<DnsResolverImpl*>(resolver_.get()));
    resetChannel();
  }

  void resetChannel() {
    if (tcpOnly()) {
      peer_->resetChannelTcpOnly(queryTimeout());
    }
    ares_set_servers_ports_csv(
        peer_->channel(), socket_->connectionInfoProvider().localAddress()->asString().c_str());
  }

  void TearDown() override {
    // Make sure we clean this up before dispatcher destruction.
    listener_.reset();
    server_.reset();
  }

  static std::list<Address::InstanceConstSharedPtr>
  getAddressList(const std::list<DnsResponse>& response) {
    std::list<Address::InstanceConstSharedPtr> address;

    for_each(response.begin(), response.end(),
             [&](DnsResponse resp) { address.emplace_back(resp.addrInfo().address_); });
    return address;
  }

  static std::list<std::string> getAddressAsStringList(const std::list<DnsResponse>& response) {
    std::list<std::string> address;

    for_each(response.begin(), response.end(), [&](DnsResponse resp) {
      address.emplace_back(resp.addrInfo().address_->ip()->addressAsString());
    });
    return address;
  }

  ActiveDnsQuery* resolveWithExpectations(const std::string& address,
                                          const DnsLookupFamily lookup_family,
                                          const DnsResolver::ResolutionStatus expected_status,
                                          const std::list<std::string>& expected_results,
                                          const std::list<std::string>& expected_absent_results,
                                          const absl::optional<std::chrono::seconds> expected_ttl) {
    return resolver_->resolve(
        address, lookup_family,
        [=, this](DnsResolver::ResolutionStatus status, absl::string_view,
                  std::list<DnsResponse>&& results) -> void {
          EXPECT_EQ(expected_status, status);

          std::list<std::string> address_as_string_list = getAddressAsStringList(results);
          // Note localhost is getting a special treatment here due to circle ci's hosts file.
          // If the coverage job is moved from circle, this can be simplified to only the exact
          // list match.
          // https://github.com/envoyproxy/envoy/pull/10137#issuecomment-592525544
          if (address == "localhost" && lookup_family == DnsLookupFamily::V4Only) {
            EXPECT_THAT(address_as_string_list, IsSupersetOf(expected_results));
          } else {
            EXPECT_THAT(address_as_string_list, UnorderedElementsAreArray(expected_results));
          }

          for (const auto& expected_absent_result : expected_absent_results) {
            EXPECT_THAT(address_as_string_list, Not(Contains(expected_absent_result)));
          }

          if (expected_ttl) {
            std::list<Address::InstanceConstSharedPtr> address_list = getAddressList(results);
            for (const auto& address : results) {
              EXPECT_EQ(address.addrInfo().ttl_, expected_ttl.value());
            }
          }

          dispatcher_->exit();
        });
  }

  ActiveDnsQuery* resolveWithNoRecordsExpectation(const std::string& address,
                                                  const DnsLookupFamily lookup_family) {
    return resolver_->resolve(address, lookup_family,
                              [=, this](DnsResolver::ResolutionStatus status, absl::string_view,
                                        std::list<DnsResponse>&& results) -> void {
                                EXPECT_EQ(DnsResolver::ResolutionStatus::Completed, status);
                                std::list<std::string> address_as_string_list =
                                    getAddressAsStringList(results);
                                EXPECT_EQ(0, address_as_string_list.size());
                                dispatcher_->exit();
                              });
  }

  ActiveDnsQuery* resolveWithUnreferencedParameters(const std::string& address,
                                                    const DnsLookupFamily lookup_family,
                                                    bool expected_to_execute) {
    return resolver_->resolve(address, lookup_family,
                              [expected_to_execute](DnsResolver::ResolutionStatus status,
                                                    absl::string_view,
                                                    std::list<DnsResponse>&& results) -> void {
                                EXPECT_TRUE(expected_to_execute);
                                UNREFERENCED_PARAMETER(status);
                                UNREFERENCED_PARAMETER(results);
                              });
  }

  template <typename T>
  ActiveDnsQuery* resolveWithException(const std::string& address,
                                       const DnsLookupFamily lookup_family, T exception_object) {
    return resolver_->resolve(address, lookup_family,
                              [exception_object](DnsResolver::ResolutionStatus status,
                                                 absl::string_view,
                                                 std::list<DnsResponse>&& results) -> void {
                                UNREFERENCED_PARAMETER(status);
                                UNREFERENCED_PARAMETER(results);
                                throw exception_object;
                              });
  }

  void testFilterAddresses(const std::vector<std::string>& ifaddrs,
                           const DnsLookupFamily lookup_family,
                           const std::list<std::string>& expected_addresses,
                           const DnsResolver::ResolutionStatus resolution_status =
                               DnsResolver::ResolutionStatus::Completed,
                           const bool getifaddrs_supported = true,
                           const bool getifaddrs_success = true) {
    server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
    server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

    auto& real_syscall = Api::OsSysCallsSingleton::get();
    Api::MockOsSysCalls os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

    EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillOnce(Return(getifaddrs_supported));
    if (getifaddrs_supported) {
      if (getifaddrs_success) {
        EXPECT_CALL(os_sys_calls, getifaddrs(_))
            .WillOnce(Invoke([&](Api::InterfaceAddressVector& vector) -> Api::SysCallIntResult {
              for (uint32_t i = 0; i < ifaddrs.size(); i++) {
                auto addr = Network::Utility::parseInternetAddressAndPortNoThrow(ifaddrs[i]);
                vector.emplace_back(fmt::format("interface_{}", i), 0, addr);
              }
              return {0, 0};
            }));
      } else {
        EXPECT_CALL(os_sys_calls, getifaddrs(_))
            .WillOnce(Invoke([&](Api::InterfaceAddressVector&) -> Api::SysCallIntResult {
              return {-1, 1};
            }));
      }
    }

    // These passthrough calls are needed to let the resolver communicate with the DNS server
    EXPECT_CALL(os_sys_calls, accept(_, _, _))
        .WillRepeatedly(Invoke(
            [&](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
              return real_syscall.accept(sockfd, addr, addrlen);
            }));
    EXPECT_CALL(os_sys_calls, readv(_, _, _))
        .WillRepeatedly(
            Invoke([&](os_fd_t fd, const iovec* iov, int num_iov) -> Api::SysCallSizeResult {
              return real_syscall.readv(fd, iov, num_iov);
            }));
    EXPECT_CALL(os_sys_calls, writev(_, _, _))
        .WillRepeatedly(
            Invoke([&](os_fd_t fd, const iovec* iov, int num_iov) -> Api::SysCallSizeResult {
              return real_syscall.writev(fd, iov, num_iov);
            }));
    EXPECT_CALL(os_sys_calls, send(_, _, _, _))
        .WillRepeatedly(Invoke(
            [&](os_fd_t socket, void* buffer, size_t length, int flags) -> Api::SysCallSizeResult {
              return real_syscall.send(socket, buffer, length, flags);
            }));
    EXPECT_CALL(os_sys_calls, close(_))
        .WillRepeatedly(Invoke(
            [&](os_fd_t sockfd) -> Api::SysCallIntResult { return real_syscall.close(sockfd); }));
    EXPECT_CALL(os_sys_calls, getaddrinfo(_, _, _, _))
        .WillRepeatedly(Invoke([&](const char* node, const char* service,
                                   const struct addrinfo* hints, struct addrinfo** res) {
          return real_syscall.getaddrinfo(node, service, hints, res);
        }));

    resolveWithExpectations("some.good.domain", lookup_family, resolution_status,
                            expected_addresses, {}, absl::nullopt);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void checkStats(uint64_t resolve_total, uint64_t pending_resolutions, uint64_t not_found,
                  uint64_t get_addr_failure, uint64_t timeouts) {
    EXPECT_EQ(resolve_total, stats_store_.counter("dns.cares.resolve_total").value());
    EXPECT_EQ(
        pending_resolutions,
        stats_store_.gauge("dns.cares.pending_resolutions", Stats::Gauge::ImportMode::NeverImport)
            .value());
    EXPECT_EQ(not_found, stats_store_.counter("dns.cares.not_found").value());
    EXPECT_EQ(get_addr_failure, stats_store_.counter("dns.cares.get_addr_failure").value());
    EXPECT_EQ(timeouts, stats_store_.counter("dns.cares.timeouts").value());
  }

protected:
  // Should the TestDnsServer cause c-ares queries to timeout, by not responding?
  virtual bool queryTimeout() const { return false; }
  virtual bool tcpOnly() const { return true; }
  virtual void updateDnsResolverOptions(){};
  virtual bool setResolverInConstructor() const { return false; }
  virtual bool filterUnroutableFamilies() const { return false; }
  virtual ProtobufWkt::UInt32Value* udpMaxQueries() const { return 0; }
  Stats::TestUtil::TestStore stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<TestDnsServer> server_;
  std::unique_ptr<DnsResolverImplPeer> peer_;
  std::shared_ptr<Network::TcpListenSocket> socket_;
  std::unique_ptr<Network::Listener> listener_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverSharedPtr resolver_;
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options_;
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that when DnsResolverImpl is destructed with outstanding requests,
// that we don't invoke any callbacks if the query was cancelled. This is a regression test from
// development, where segfaults were encountered due to callback invocations on
// destruction.
TEST_P(DnsImplTest, DestructPending) {
  ActiveDnsQuery* query =
      resolveWithUnreferencedParameters("foo.bar.baz", DnsLookupFamily::V4Only, false);
  ASSERT_NE(nullptr, query);
  query->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
  // Also validate that pending events are around to exercise the resource
  // reclamation path.
  EXPECT_GT(peer_->events().size(), 0U);
}

// Validate that All queries (2 concurrent queries) properly cleanup when the channel is destroyed.
// TODO(mattklein123): This is a brute force way of testing this path, however we have seen
// evidence that this happens during normal operation via the "channel dirty" path. The sequence
// must look something like:
// 1) Issue All query with 2 parallel sub-queries.
// 2) Issue parallel query which fails with ARES_ECONNREFUSED, mark channel dirty.
// 3) Issue 3rd query which sees a dirty channel and destroys it, causing the parallel queries
//    issued in step 1 to fail. It's not clear whether this is possible over a TCP channel or
//    just via UDP.
// Either way, we have no tests today that cover parallel queries. We can do this is a follow up.
TEST_P(DnsImplTest, DestructPendingAllQuery) {
  ActiveDnsQuery* query =
      resolveWithUnreferencedParameters("foo.bar.baz", DnsLookupFamily::All, true);
  ASSERT_NE(nullptr, query);
}

TEST_P(DnsImplTest, DestructCallback) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.domain", DnsLookupFamily::Auto,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));

  // This simulates destruction thanks to another query setting the dirty_channel_ bit, thus causing
  // a subsequent result to call ares_destroy.
  resetChannel();

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate basic success/fail lookup behavior. The "foo.bar.baz" request will connect
// to TestDnsServer, but localhost should resolve via the hosts file with no
// asynchronous behavior or network events.
TEST_P(DnsImplTest, LocalLookup) {
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("foo.bar.baz", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  if (GetParam() == Address::IpVersion::v4) {
    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::V4Only,
                                               DnsResolver::ResolutionStatus::Completed,
                                               {"127.0.0.1"}, {"::1"}, absl::nullopt));
  }

  if (GetParam() == Address::IpVersion::v6) {
    const std::string error_msg =
        "Synchronous DNS IPv6 localhost resolution failed. Please verify localhost resolves to ::1 "
        "in /etc/hosts, since this misconfiguration is a common cause of these failures.";
    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::V6Only,
                                               DnsResolver::ResolutionStatus::Completed, {"::1"},
                                               {"127.0.0.1"}, absl::nullopt))
        << error_msg;

    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::Auto,
                                               DnsResolver::ResolutionStatus::Completed, {"::1"},
                                               {"127.0.0.1"}, absl::nullopt))
        << error_msg;
  }
}

TEST_P(DnsImplTest, DnsIpAddressVersion) {
  server_->addHosts("some.good.domain", {"1.2.3.4"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {"1.2.3.4"},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed, {"1.2.3.4"},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V6Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, DnsIpAddressVersionV6) {
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate exception behavior during c-ares callbacks.
TEST_P(DnsImplTest, CallbackException) {
  // Force immediate resolution, which will trigger a c-ares exception unsafe
  // state providing regression coverage for #4307.
  EXPECT_EQ(nullptr, resolveWithException<EnvoyException>("1.2.3.4", DnsLookupFamily::V4Only,
                                                          EnvoyException("Envoy exception")));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "Envoy exception");
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_EQ(nullptr,
            resolveWithException<std::string>("1.2.3.4", DnsLookupFamily::V4Only, std::string()));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "unknown");
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Verify that resetNetworking() correctly dirties and recreates the channel.
TEST_P(DnsImplTest, DestroyChannelOnResetNetworking) {
  ASSERT_FALSE(peer_->isChannelDirty());
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  resolver_->resetNetworking();
  EXPECT_TRUE(peer_->isChannelDirty());
  resetChannel();

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate that the c-ares channel is destroyed and re-initialized when c-ares returns
// ARES_ECONNREFUSED as its callback status.
TEST_P(DnsImplTest, DestroyChannelOnRefused) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  ASSERT_FALSE(peer_->isChannelDirty());
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->setRefused(true);

  EXPECT_NE(nullptr,
            resolveWithExpectations("unresolvable.name", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  // The c-ares channel should be dirty because the TestDnsServer replied with return code REFUSED;
  // This test, and the way the TestDnsServerQuery is setup, relies on the fact that Envoy's
  // c-ares channel is configured **without** the ARES_FLAG_NOCHECKRESP flag. This causes c-ares to
  // discard packets with REFUSED, and thus Envoy receives ARES_ECONNREFUSED due to the code here:
  // https://github.com/c-ares/c-ares/blob/d7e070e7283f822b1d2787903cce3615536c5610/ares_process.c#L654
  // If that flag needs to be set, or c-ares changes its handling this test will need to be updated
  // to create another condition where c-ares invokes onAresGetAddrInfoCallback with status ==
  // ARES_ECONNREFUSED.
  EXPECT_TRUE(peer_->isChannelDirty());

  server_->setRefused(false);

  // Resolve will destroy the original channel and create a new one.
  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // However, the fresh channel initialized by production code does not point to the TestDnsServer.
  // This means that resolution will return `ARES_ENOTFOUND`. This should not dirty the channel.
  EXPECT_FALSE(peer_->isChannelDirty());

  // Reset the channel to point to the TestDnsServer, and make sure resolution is healthy.
  resetChannel();
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(peer_->isChannelDirty());
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate completed/fail lookup behavior via TestDnsServer. This exercises the
// network event handling in DnsResolverImpl.
TEST_P(DnsImplTest, RemoteAsyncLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.bad.domain", DnsLookupFamily::Auto));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 3 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate that multiple A records are correctly passed to the callback.
TEST_P(DnsImplTest, MultiARecordLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, RecordType::A);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                    DnsResolver::ResolutionStatus::Completed,
                                    {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, CNameARecordLookupV4) {
  server_->addCName("root.cnam.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  server_->setRecordTtl(std::chrono::seconds(300));
  server_->setCnameTtl(std::chrono::seconds(60));

  EXPECT_NE(nullptr, resolveWithExpectations("root.cnam.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, std::chrono::seconds(60)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, CNameARecordLookupWithV6) {
  server_->addCName("root.cnam.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  server_->setRecordTtl(std::chrono::seconds(300));
  server_->setCnameTtl(std::chrono::seconds(60));

  EXPECT_NE(nullptr, resolveWithExpectations("root.cnam.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, std::chrono::seconds(60)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// RFC 2181: TTL values can be between [0, 2^31-1]
TEST_P(DnsImplTest, CNameARecordLookupV4InvalidTTL) {
  server_->addCName("root.cnam.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);

  // Case 1: Negative TTL
  server_->setRecordTtl(std::chrono::seconds(-5));
  server_->setCnameTtl(std::chrono::seconds(60));

  EXPECT_NE(nullptr, resolveWithExpectations("root.cnam.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, std::chrono::seconds(0)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Case 2: TTL Overflow
  server_->setRecordTtl(std::chrono::seconds(2147483648));
  server_->setCnameTtl(std::chrono::seconds(2147483648));

  EXPECT_NE(nullptr, resolveWithExpectations("root.cnam.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, std::chrono::seconds(0)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Case 3: Max TTL
  server_->setRecordTtl(std::chrono::seconds(2147483647));
  server_->setCnameTtl(std::chrono::seconds(2147483647));

  EXPECT_NE(nullptr,
            resolveWithExpectations("root.cnam.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Completed, {"201.134.56.7"}, {},
                                    std::chrono::seconds(2147483647)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, MultiARecordLookupWithV6) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2", "1::2:3", "1::2:3:4"}, RecordType::AAAA);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Completed,
                                    {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"1::2", "1::2:3", "1::2:3:4"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"1::2", "1::2:3", "1::2:3:4"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AutoOnlyV6IfBothV6andV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {{"1::2"}},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AutoV6IfOnlyV6) {
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {{"1::2"}},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AutoV4IfOnlyV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, V4PreferredOnlyV4IfBothV6andV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, V4PreferredV6IfOnlyV6) {
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed, {{"1::2"}},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, V4PreferredV4IfOnlyV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AllIfBothV6andV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}, {"1::2"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AllV6IfOnlyV6) {
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed, {{"1::2"}},
                                             {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, AllV4IfOnlyV4) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_P(DnsImplTest, Cancel) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);

  ActiveDnsQuery* query =
      resolveWithUnreferencedParameters("some.domain", DnsLookupFamily::Auto, false);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));

  ASSERT_NE(nullptr, query);
  query->cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned);

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  if (stats_store_.counter("dns.cares.resolve_total").value() < 4) {
    // if c-ares did not read both responses at once, run another loop iteration
    // to make it read the second response.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 3 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate working of querying ttl of resource record.
TEST_P(DnsImplTest, RecordTtlLookup) {
  uint64_t resolve_total = 0;
  if (GetParam() == Address::IpVersion::v4) {
    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::V4Only,
                                               DnsResolver::ResolutionStatus::Completed,
                                               {"127.0.0.1"}, {}, std::chrono::seconds(0)));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
               0 /*get_addr_failure*/, 0 /*timeouts*/);
    resolve_total++;
  }

  if (GetParam() == Address::IpVersion::v6) {
    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::V6Only,
                                               DnsResolver::ResolutionStatus::Completed, {"::1"},
                                               {}, std::chrono::seconds(0)));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
               0 /*get_addr_failure*/, 0 /*timeouts*/);
    resolve_total++;

    EXPECT_EQ(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::Auto,
                                               DnsResolver::ResolutionStatus::Completed, {"::1"},
                                               {}, std::chrono::seconds(0)));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
               0 /*get_addr_failure*/, 0 /*timeouts*/);
    resolve_total++;
  }

  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2", "1::2:3", "1::2:3:4"}, RecordType::AAAA);
  server_->setRecordTtl(std::chrono::seconds(300));

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Completed,
                                    {"201.134.56.7", "123.4.5.6", "6.5.4.3"},
                                    {"1::2", "1::2:3", "1::2:3:4"}, std::chrono::seconds(300)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
  resolve_total++;

  EXPECT_NE(nullptr, resolveWithExpectations(
                         "some.good.domain", DnsLookupFamily::Auto,
                         DnsResolver::ResolutionStatus::Completed, {"1::2", "1::2:3", "1::2:3:4"},
                         {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, std::chrono::seconds(300)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
  resolve_total++;

  EXPECT_NE(nullptr, resolveWithExpectations(
                         "some.good.domain", DnsLookupFamily::V6Only,
                         DnsResolver::ResolutionStatus::Completed, {"1::2", "1::2:3", "1::2:3:4"},
                         {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, std::chrono::seconds(300)));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
  resolve_total++;

  server_->addHosts("domain.onion", {"1.2.3.4"}, RecordType::A);
  server_->addHosts("domain.onion.", {"2.3.4.5"}, RecordType::A);

  // test onion domain
  EXPECT_EQ(nullptr, resolveWithNoRecordsExpectation("domain.onion", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
  resolve_total++;

  EXPECT_EQ(nullptr, resolveWithNoRecordsExpectation("domain.onion.", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(resolve_total + 1 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate that the resolution timeout timer is enabled if we don't resolve
// immediately.
TEST_P(DnsImplTest, PendingTimerEnable) {
  InSequence s;
  const envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig config;
  Event::MockDispatcher dispatcher;
  Event::MockTimer* timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(Return(timer));
  resolver_ = std::make_shared<DnsResolverImpl>(config, dispatcher, "", *stats_store_.rootScope());
  Event::FileEvent* file_event = new NiceMock<Event::MockFileEvent>();
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event));
  EXPECT_CALL(*timer, enableTimer(_, _));
  EXPECT_NE(nullptr, resolveWithUnreferencedParameters("some.bad.domain.invalid",
                                                       DnsLookupFamily::V4Only, true));
  checkStats(0 /*resolve_total*/, 1 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

// Validate that the pending resolutions stat is reset.
TEST_P(DnsImplTest, PendingResolutions) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {{"201.134.56.7"}}, {}, absl::nullopt));

  checkStats(0 /*resolve_total*/, 1 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, WithNoRecord) {
  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V6Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::Auto));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 4 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Preferred));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 6 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::All));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(7 /*resolve_total*/, 0 /*pending_resolutions*/, 7 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, WithARecord) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V6Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(5 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, WithAAAARecord) {
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);
  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(5 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, WithBothAAndAAAARecord) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->addHosts("some.good.domain", {"1::2"}, RecordType::AAAA);
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed, {"1::2"}, {},
                                             absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(3 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7", "1::2"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(5 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, FallbackToNodataWithErrorOnA) {
  server_->setErrorOnQtypeA(true);
  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V6Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::Auto));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             2 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Preferred));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 3 /*not_found*/,
             3 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::All));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(7 /*resolve_total*/, 0 /*pending_resolutions*/, 4 /*not_found*/,
             3 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, FallbackToNodataWithErrorOnAAAA) {
  server_->setErrorOnQtypeAAAA(true);
  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Only));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr, resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::Auto));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 2 /*not_found*/,
             2 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithNoRecordsExpectation("some.good.domain", DnsLookupFamily::V4Preferred));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 3 /*not_found*/,
             3 /*get_addr_failure*/, 0 /*timeouts*/);

  // When DnsLookupFamily::All is provided, both IPv4 and IPv6 queries are sent by c-ares in the
  // same ares_getaddrinfo operation. If one of them fails with `FORMERR`, the whole operation
  // will fail.
  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(7 /*resolve_total*/, 0 /*pending_resolutions*/, 3 /*not_found*/,
             4 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplTest, ErrorWithAcceptNodataEnabled) {
  server_->setErrorOnQtypeA(true);
  server_->setErrorOnQtypeAAAA(true);
  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V6Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             2 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(4 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             4 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Preferred,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(6 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             6 /*get_addr_failure*/, 0 /*timeouts*/);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::All,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(7 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             7 /*get_addr_failure*/, 0 /*timeouts*/);
}

class DnsImplFilterUnroutableFamiliesTest : public DnsImplTest {
protected:
  bool filterUnroutableFamilies() const override { return true; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplFilterUnroutableFamiliesTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterUnroutable) {
  testFilterAddresses({}, DnsLookupFamily::Auto, {}, DnsResolver::ResolutionStatus::Failure);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             2 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterUnroutableV6) {
  // Auto would have preferred V6, but because there is no v6 interface we will get v4.
  testFilterAddresses({"1.2.3.4:80"}, DnsLookupFamily::Auto, {"201.134.56.7"});
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterUnroutableV6LoopbackDoesntCount) {
  testFilterAddresses({"1.2.3.4:80", "[::1]:80"}, DnsLookupFamily::Auto, {"201.134.56.7"});
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, DontFilterV6) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::Auto, {"1::2"});
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterUnroutableV4) {
  // V4Preferred would have preferred v4, but because there is no v6 interface we will get v4.
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::V4Preferred, {"1::2"});
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterUnroutableV4LoopbackDoesntCount) {
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54", "127.0.0.1:80"},
                      DnsLookupFamily::V4Preferred, {"1::2"});
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, DontFilterV4) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::V4Preferred, {"201.134.56.7"});
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, DontFilterAll) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::All, {"201.134.56.7", "1::2"});
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterAllV4) {
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"}, DnsLookupFamily::All,
                      {"1::2"});
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, FilterAllV6) {
  testFilterAddresses({"1.2.3.4:80"}, DnsLookupFamily::All, {"201.134.56.7"});
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, DontFilterIfGetifaddrsIsNotSupported) {
  testFilterAddresses({}, DnsLookupFamily::All, {"201.134.56.7", "1::2"},
                      DnsResolver::ResolutionStatus::Completed, false /* getifaddrs_supported */);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_P(DnsImplFilterUnroutableFamiliesTest, DontFilterIfThereIsAGetifaddrsFailure) {
  testFilterAddresses({}, DnsLookupFamily::All, {"201.134.56.7", "1::2"},
                      DnsResolver::ResolutionStatus::Completed, true /* getifaddrs_supported */,
                      false /* getifaddrs_success */);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

class DnsImplFilterUnroutableFamiliesDontFilterTest : public DnsImplTest {
protected:
  bool filterUnroutableFamilies() const override { return false; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplFilterUnroutableFamiliesDontFilterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, FilterUnroutableV6) {
  testFilterAddresses({"1.2.3.4:80"}, DnsLookupFamily::Auto, {"1::2"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, FilterUnroutableV6LoopbackDoesntCount) {
  testFilterAddresses({"1.2.3.4:80", "[::1]:80"}, DnsLookupFamily::Auto, {"1::2"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, DontFilterV6) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::Auto, {"1::2"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, FilterUnroutableV4) {
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::V4Preferred, {"201.134.56.7"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, FilterUnroutableV4LoopbackDoesntCount) {
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54", "127.0.0.1:80"},
                      DnsLookupFamily::V4Preferred, {"201.134.56.7"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, DontFilterV4) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::V4Preferred, {"201.134.56.7"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, DontFilterAll) {
  testFilterAddresses({"1.2.3.4:80", "[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"},
                      DnsLookupFamily::All, {"201.134.56.7", "1::2"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, DontFilterAllV4) {
  testFilterAddresses({"[2001:0000:3238:DFE1:0063:0000:0000:FEFB]:54"}, DnsLookupFamily::All,
                      {"201.134.56.7", "1::2"});
}

TEST_P(DnsImplFilterUnroutableFamiliesDontFilterTest, DontFilterAllV6) {
  testFilterAddresses({"1.2.3.4:80"}, DnsLookupFamily::All, {"201.134.56.7", "1::2"});
}

class DnsImplZeroTimeoutTest : public DnsImplTest {
protected:
  bool queryTimeout() const override { return true; }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplZeroTimeoutTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that timeouts result in an empty callback.
TEST_P(DnsImplZeroTimeoutTest, Timeout) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 1 /*timeouts*/);
}

class DnsImplAresFlagsForTcpTest : public DnsImplTest {
protected:
  bool tcpOnly() const override { return false; }
  void updateDnsResolverOptions() override {
    dns_resolver_options_.set_use_tcp_for_dns_lookups(true);
  }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplAresFlagsForTcpTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that c_ares flag `ARES_FLAG_USEVC` is set when boolean property
// `use_tcp_for_dns_lookups` is enabled.
TEST_P(DnsImplAresFlagsForTcpTest, TcpLookupsEnabled) {
  server_->addCName("root.cname.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  ares_options opts{};
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(peer_->channel(), &opts, &optmask));
  EXPECT_TRUE((opts.flags & ARES_FLAG_USEVC) == ARES_FLAG_USEVC);
  EXPECT_NE(nullptr,
            resolveWithUnreferencedParameters("root.cname.domain", DnsLookupFamily::Auto, true));
  ares_destroy_options(&opts);
}

class DnsImplAresFlagsForNoDefaultSearchDomainTest : public DnsImplTest {
protected:
  bool tcpOnly() const override { return false; }
  void updateDnsResolverOptions() override {
    dns_resolver_options_.set_no_default_search_domain(true);
  }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplAresFlagsForNoDefaultSearchDomainTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that c_ares flag `ARES_FLAG_NOSEARCH` is set when boolean property
// `no_default_search_domain` is enabled.
TEST_P(DnsImplAresFlagsForNoDefaultSearchDomainTest, NoDefaultSearchDomainSet) {
  server_->addCName("root.cname.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  ares_options opts{};
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(peer_->channel(), &opts, &optmask));
  EXPECT_TRUE((opts.flags & ARES_FLAG_NOSEARCH) == ARES_FLAG_NOSEARCH);
  EXPECT_NE(nullptr,
            resolveWithUnreferencedParameters("root.cname.domain", DnsLookupFamily::Auto, true));
  ares_destroy_options(&opts);
}

class DnsImplAresFlagsForUdpTest : public DnsImplTest {
protected:
  bool tcpOnly() const override { return false; }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplAresFlagsForUdpTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that c_ares flag `ARES_FLAG_USEVC` is not set when boolean property
// `use_tcp_for_dns_lookups` is disabled.
TEST_P(DnsImplAresFlagsForUdpTest, UdpLookupsEnabled) {
  server_->addCName("root.cname.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  ares_options opts{};
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(peer_->channel(), &opts, &optmask));
  EXPECT_FALSE((opts.flags & ARES_FLAG_USEVC) == ARES_FLAG_USEVC);
  EXPECT_NE(nullptr,
            resolveWithUnreferencedParameters("root.cname.domain", DnsLookupFamily::Auto, true));
  ares_destroy_options(&opts);
}

class DnsImplAresFlagsForDefaultSearchDomainTest : public DnsImplTest {
protected:
  bool tcpOnly() const override { return false; }
  void updateDnsResolverOptions() override {}
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplAresFlagsForDefaultSearchDomainTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that c_ares flag `ARES_FLAG_NOSEARCH` is not set when boolean property
// `no_default_search_domain` is disabled.
TEST_P(DnsImplAresFlagsForDefaultSearchDomainTest, NoDefaultSearchDomainNotSet) {
  server_->addCName("root.cname.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  ares_options opts{};
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(peer_->channel(), &opts, &optmask));
  EXPECT_FALSE((opts.flags & ARES_FLAG_NOSEARCH) == ARES_FLAG_NOSEARCH);
  EXPECT_NE(nullptr,
            resolveWithUnreferencedParameters("root.cname.domain", DnsLookupFamily::Auto, true));
  ares_destroy_options(&opts);
}

class DnsImplCustomResolverTest : public DnsImplTest {
  bool tcpOnly() const override { return false; }
  void updateDnsResolverOptions() override {
    dns_resolver_options_.set_use_tcp_for_dns_lookups(true);
  }
  bool setResolverInConstructor() const override { return true; }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplCustomResolverTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsImplCustomResolverTest, CustomResolverValidAfterChannelDestruction) {
  ASSERT_FALSE(peer_->isChannelDirty());
  server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
  server_->setRefused(true);

  EXPECT_NE(nullptr,
            resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
                                    DnsResolver::ResolutionStatus::Failure, {}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // The c-ares channel should be dirty because the TestDnsServer replied with return code REFUSED;
  // This test, and the way the TestDnsServerQuery is setup, relies on the fact that Envoy's
  // c-ares channel is configured **without** the ARES_FLAG_NOCHECKRESP flag. This causes c-ares to
  // discard packets with REFUSED, and thus Envoy receives ARES_ECONNREFUSED due to the code here:
  // https://github.com/c-ares/c-ares/blob/d7e070e7283f822b1d2787903cce3615536c5610/ares_process.c#L654
  // If that flag needs to be set, or c-ares changes its handling this test will need to be updated
  // to create another condition where c-ares invokes onAresGetAddrInfoCallback with status ==
  // ARES_ECONNREFUSED.
  EXPECT_TRUE(peer_->isChannelDirty());

  server_->setRefused(false);

  // The next query destroys, and re-initializes the channel. Furthermore, because the test dns
  // server's address was passed as a custom resolver on construction, the new channel should still
  // point to the test dns server, and the query should succeed.
  EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Completed,
                                             {"201.134.56.7"}, {}, absl::nullopt));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(peer_->isChannelDirty());
}

class DnsImplAresFlagsForMaxUdpQueriesinTest : public DnsImplTest {
protected:
  bool tcpOnly() const override { return false; }
  ProtobufWkt::UInt32Value* udpMaxQueries() const override {
    auto udp_max_queries = std::make_unique<ProtobufWkt::UInt32Value>();
    udp_max_queries->set_value(100);
    return dynamic_cast<ProtobufWkt::UInt32Value*>(udp_max_queries.release());
  }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplAresFlagsForMaxUdpQueriesinTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate that c_ares flag `ARES_OPT_UDP_MAX_QUERIES` is set when UInt32 property
// `udp_max_queries` is set.
TEST_P(DnsImplAresFlagsForMaxUdpQueriesinTest, UdpMaxQueriesIsSet) {
  server_->addCName("root.cname.domain", "result.cname.domain");
  server_->addHosts("result.cname.domain", {"201.134.56.7"}, RecordType::A);
  ares_options opts{};
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(peer_->channel(), &opts, &optmask));
  EXPECT_TRUE((optmask & ARES_OPT_UDP_MAX_QUERIES) == ARES_OPT_UDP_MAX_QUERIES);
  EXPECT_TRUE(opts.udp_max_queries == 100);
  EXPECT_NE(nullptr,
            resolveWithUnreferencedParameters("root.cname.domain", DnsLookupFamily::Auto, true));
  ares_destroy_options(&opts);
}

} // namespace Network
} // namespace Envoy
