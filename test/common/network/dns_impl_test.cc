#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"

#include "ares.h"
#include "ares_dns.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {

namespace {

// List of IP address (in human readable format).
typedef std::list<std::string> IpList;
// Map from hostname to IpList.
typedef std::unordered_map<std::string, IpList> HostMap;
// Represents a single TestDnsServer query state and lifecycle. This implements
// just enough of RFC 1035 to handle queries we generate in the tests below.
enum record_type { A, AAAA };

class TestDnsServerQuery {
public:
  TestDnsServerQuery(ConnectionPtr connection, const HostMap& hosts_A, const HostMap& hosts_AAAA)
      : connection_(std::move(connection)), hosts_A_(hosts_A), hosts_AAAA_(hosts_AAAA) {
    connection_->addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
  }

  ~TestDnsServerQuery() { connection_->close(ConnectionCloseType::NoFlush); }

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
        // the hosts_A_ host map. If the query type is of type AAAA, then perform the
        // lookup in the hosts_AAAA_ host map.
        char* name;
        ASSERT_EQ(ARES_SUCCESS, ares_expand_name(question, request, size_, &name, &name_len));
        const std::list<std::string>* ips = nullptr;
        // We only expect resources of type A or AAAA.
        const int q_type = DNS_QUESTION_TYPE(question + name_len);
        ASSERT_TRUE(q_type == T_A || q_type == T_AAAA);
        if (q_type == T_A) {
          auto it = parent_.hosts_A_.find(name);
          if (it != parent_.hosts_A_.end()) {
            ips = &it->second;
          }
        } else {
          auto it = parent_.hosts_AAAA_.find(name);
          if (it != parent_.hosts_AAAA_.end()) {
            ips = &it->second;
          }
        }
        ares_free_string(name);

        // The response begins with the intial part of the request
        // (including the question section).
        const size_t response_base_len = HFIXEDSZ + name_len + QFIXEDSZ;
        unsigned char response_base[response_base_len];
        memcpy(response_base, request, response_base_len);
        DNS_HEADER_SET_QR(response_base, 1);
        DNS_HEADER_SET_AA(response_base, 0);
        DNS_HEADER_SET_RCODE(response_base, ips != nullptr ? NOERROR : NXDOMAIN);
        DNS_HEADER_SET_ANCOUNT(response_base, ips != nullptr ? ips->size() : 0);
        DNS_HEADER_SET_NSCOUNT(response_base, 0);
        DNS_HEADER_SET_ARCOUNT(response_base, 0);

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
        DNS_RR_SET_TTL(response_rr_fixed, 0);

        size_t response_rest_len;
        if (q_type == T_A) {
          response_rest_len =
              ips != nullptr ? ips->size() * (name_len + RRFIXEDSZ + sizeof(in_addr)) : 0;
        } else {
          response_rest_len =
              ips != nullptr ? ips->size() * (name_len + RRFIXEDSZ + sizeof(in6_addr)) : 0;
        }
        // Send response to client.
        const uint16_t response_size_n = htons(response_base_len + response_rest_len);
        Buffer::OwnedImpl write_buffer_;
        write_buffer_.add(&response_size_n, sizeof(response_size_n));
        write_buffer_.add(response_base, response_base_len);
        if (ips != nullptr) {
          for (const auto& it : *ips) {
            write_buffer_.add(question, name_len);
            write_buffer_.add(response_rr_fixed, RRFIXEDSZ);
            if (q_type == T_A) {
              in_addr addr;
              ASSERT_EQ(1, inet_pton(AF_INET, it.c_str(), &addr));
              write_buffer_.add(&addr, sizeof(addr));
            } else {
              in6_addr addr;
              ASSERT_EQ(1, inet_pton(AF_INET6, it.c_str(), &addr));
              write_buffer_.add(&addr, sizeof(addr));
            }
          }
        }
        parent_.connection_->write(write_buffer_, false);

        // Reset query state, time for the next one.
        buffer_.drain(size_);
        size_ = 0;
      }
      return;
    }

    TestDnsServerQuery& parent_;
    // The expected size of the current DNS query to read. If zero, indicates that
    // no DNS query is in progress and that a 2 byte size is expected from the
    // client to indicate the next DNS query size.
    uint16_t size_ = 0;
    Buffer::OwnedImpl buffer_;
  };

private:
  ConnectionPtr connection_;
  const HostMap& hosts_A_;
  const HostMap& hosts_AAAA_;
};

class TestDnsServer : public ListenerCallbacks {
public:
  TestDnsServer(Event::DispatcherImpl& dispatcher) : dispatcher_(dispatcher) {}

  void onAccept(ConnectionSocketPtr&& socket, bool) override {
    Network::ConnectionPtr new_connection = dispatcher_.createServerConnection(
        std::move(socket), Network::Test::createRawBufferSocket());
    onNewConnection(std::move(new_connection));
  }

  void onNewConnection(ConnectionPtr&& new_connection) override {
    TestDnsServerQuery* query =
        new TestDnsServerQuery(std::move(new_connection), hosts_A_, hosts_AAAA_);
    queries_.emplace_back(query);
  }

  void addHosts(const std::string& hostname, const IpList& ip, const record_type& type) {
    if (type == A) {
      hosts_A_[hostname] = ip;
    } else if (type == AAAA) {
      hosts_AAAA_[hostname] = ip;
    }
  }

private:
  Event::DispatcherImpl& dispatcher_;

  HostMap hosts_A_;
  HostMap hosts_AAAA_;
  // All queries are tracked so we can do resource reclamation when the test is
  // over.
  std::vector<std::unique_ptr<TestDnsServerQuery>> queries_;
};
} // namespace

class DnsResolverImplPeer {
public:
  DnsResolverImplPeer(DnsResolverImpl* resolver) : resolver_(resolver) {}
  ares_channel channel() const { return resolver_->channel_; }
  const std::unordered_map<int, Event::FileEventPtr>& events() { return resolver_->events_; }
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

private:
  DnsResolverImpl* resolver_;
};

TEST(DnsImplConstructor, SupportsCustomResolvers) {
  Event::DispatcherImpl dispatcher;
  char addr4str[INET_ADDRSTRLEN];
  // we pick a port that isn't 53 as the default resolve.conf might be
  // set to point to localhost.
  auto addr4 = Network::Utility::parseInternetAddressAndPort("127.0.0.1:54");
  char addr6str[INET6_ADDRSTRLEN];
  auto addr6 = Network::Utility::parseInternetAddressAndPort("[::1]:54");
  auto resolver = dispatcher.createDnsResolver({addr4, addr6});
  auto peer = std::unique_ptr<DnsResolverImplPeer>{
      new DnsResolverImplPeer(dynamic_cast<DnsResolverImpl*>(resolver.get()))};
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

class DnsImplTest : public testing::TestWithParam<Address::IpVersion> {
public:
  void SetUp() override {
    resolver_ = dispatcher_.createDnsResolver({});

    // Instantiate TestDnsServer and listen on a random port on the loopback address.
    server_.reset(new TestDnsServer(dispatcher_));
    socket_.reset(
        new Network::TcpListenSocket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true));
    listener_ = dispatcher_.createListener(*socket_, *server_, true, false);

    // Point c-ares at the listener with no search domains and TCP-only.
    peer_.reset(new DnsResolverImplPeer(dynamic_cast<DnsResolverImpl*>(resolver_.get())));
    peer_->resetChannelTcpOnly(zero_timeout());
    ares_set_servers_ports_csv(peer_->channel(), socket_->localAddress()->asString().c_str());
  }

  void TearDown() override {
    // Make sure we clean this up before dispatcher destruction.
    listener_.reset();
    server_.reset();
  }

protected:
  // Should the DnsResolverImpl use a zero timeout for c-ares queries?
  virtual bool zero_timeout() const { return false; }
  std::unique_ptr<TestDnsServer> server_;
  std::unique_ptr<DnsResolverImplPeer> peer_;
  Network::MockConnectionHandler connection_handler_;
  Network::TcpListenSocketPtr socket_;
  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<Network::Listener> listener_;
  Event::DispatcherImpl dispatcher_;
  DnsResolverSharedPtr resolver_;
};

static bool hasAddress(const std::list<Address::InstanceConstSharedPtr>& results,
                       const std::string& address) {
  for (const auto& result : results) {
    if (result->ip()->addressAsString() == address) {
      return true;
    }
  }
  return false;
}

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_CASE_P(IpVersions, DnsImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that when DnsResolverImpl is destructed with outstanding requests,
// that we don't invoke any callbacks. This is a regression test from
// development, where segfaults were encountered due to callback invocations on
// destruction.
TEST_P(DnsImplTest, DestructPending) {
  EXPECT_NE(nullptr,
            resolver_->resolve("", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 FAIL();
                                 UNREFERENCED_PARAMETER(results);
                               }));
  // Also validate that pending events are around to exercise the resource
  // reclamation path.
  EXPECT_GT(peer_->events().size(), 0U);
}

// Validate basic success/fail lookup behavior. The empty request will connect
// to TestDnsServer, but localhost should resolve via the hosts file with no
// asynchronous behavior or network events.
TEST_P(DnsImplTest, LocalLookup) {
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  if (GetParam() == Address::IpVersion::v4) {
    EXPECT_EQ(nullptr,
              resolver_->resolve("localhost", DnsLookupFamily::V4Only,
                                 [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                   address_list = results;
                                 }));
    EXPECT_TRUE(hasAddress(address_list, "127.0.0.1"));
    EXPECT_FALSE(hasAddress(address_list, "::1"));
  }

  if (GetParam() == Address::IpVersion::v6) {
    const std::string error_msg =
        "Synchronous DNS IPv6 localhost resolution failed. Please verify localhost resolves to ::1 "
        "in /etc/hosts, since this misconfiguration is a common cause of these failures.";
    EXPECT_EQ(nullptr,
              resolver_->resolve("localhost", DnsLookupFamily::V6Only,
                                 [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                   address_list = results;
                                 }))
        << error_msg;
    EXPECT_TRUE(hasAddress(address_list, "::1")) << error_msg;
    EXPECT_FALSE(hasAddress(address_list, "127.0.0.1"));

    EXPECT_EQ(nullptr,
              resolver_->resolve("localhost", DnsLookupFamily::Auto,
                                 [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                   address_list = results;
                                 }))
        << error_msg;
    EXPECT_FALSE(hasAddress(address_list, "127.0.0.1"));
    EXPECT_TRUE(hasAddress(address_list, "::1")) << error_msg;
  }
}

TEST_P(DnsImplTest, DnsIpAddressVersionV6) {
  std::list<Address::InstanceConstSharedPtr> address_list;
  server_->addHosts("some.good.domain", {"1::2"}, AAAA);
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1::2"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(hasAddress(address_list, "1::2"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V6Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1::2"));
}

TEST_P(DnsImplTest, DnsIpAddressVersion) {
  std::list<Address::InstanceConstSharedPtr> address_list;
  server_->addHosts("some.good.domain", {"1.2.3.4"}, A);
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1.2.3.4"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1.2.3.4"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V6Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_FALSE(hasAddress(address_list, "1.2.3.4"));
}

// Validate success/fail lookup behavior via TestDnsServer. This exercises the
// network event handling in DnsResolverImpl.
TEST_P(DnsImplTest, RemoteAsyncLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, A);
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("some.bad.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
}

// Validate that multiple A records are correctly passed to the callback.
TEST_P(DnsImplTest, MultiARecordLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, A);
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
  EXPECT_TRUE(hasAddress(address_list, "123.4.5.6"));
  EXPECT_TRUE(hasAddress(address_list, "6.5.4.3"));
}

TEST_P(DnsImplTest, MultiARecordLookupWithV6) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"}, A);
  server_->addHosts("some.good.domain", {"1::2", "1::2:3", "1::2:3:4"}, AAAA);
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
  EXPECT_TRUE(hasAddress(address_list, "123.4.5.6"));
  EXPECT_TRUE(hasAddress(address_list, "6.5.4.3"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1::2"));
  EXPECT_TRUE(hasAddress(address_list, "1::2:3"));
  EXPECT_TRUE(hasAddress(address_list, "1::2:3:4"));

  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V6Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "1::2"));
  EXPECT_TRUE(hasAddress(address_list, "1::2:3"));
  EXPECT_TRUE(hasAddress(address_list, "1::2:3:4"));
}

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_P(DnsImplTest, Cancel) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, A);

  ActiveDnsQuery* query =
      resolver_->resolve("some.domain", DnsLookupFamily::Auto,
                         [](std::list<Address::InstanceConstSharedPtr> &&) -> void { FAIL(); });

  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::Auto,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  ASSERT_NE(nullptr, query);
  query->cancel();

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
}

class DnsImplZeroTimeoutTest : public DnsImplTest {
protected:
  bool zero_timeout() const override { return true; }
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_CASE_P(IpVersions, DnsImplZeroTimeoutTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that timeouts result in an empty callback.
TEST_P(DnsImplZeroTimeoutTest, Timeout) {
  server_->addHosts("some.good.domain", {"201.134.56.7"}, A);
  std::list<Address::InstanceConstSharedPtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("some.good.domain", DnsLookupFamily::V4Only,
                               [&](std::list<Address::InstanceConstSharedPtr>&& results) -> void {
                                 address_list = results;
                                 dispatcher_.exit();
                               }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());
}

// Validate that the resolution timeout timer is enabled if we don't resolve
// immediately.
TEST(DnsImplUnitTest, PendingTimerEnable) {
  InSequence s;
  Event::MockDispatcher dispatcher;
  Event::MockTimer* timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(dispatcher, createTimer_(_)).WillOnce(Return(timer));
  DnsResolverImpl resolver(dispatcher, {});
  Event::FileEvent* file_event = new NiceMock<Event::MockFileEvent>();
  EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event));
  EXPECT_CALL(*timer, enableTimer(_));
  EXPECT_NE(nullptr, resolver.resolve("some.bad.domain.invalid", DnsLookupFamily::V4Only,
                                      [&](std::list<Address::InstanceConstSharedPtr>&& results) {
                                        UNREFERENCED_PARAMETER(results);
                                      }));
}

} // namespace Network
} // namespace Envoy
