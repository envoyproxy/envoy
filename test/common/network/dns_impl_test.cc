#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"

#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>

#include "ares.h"
#include "ares_dns.h"

namespace Network {

namespace {

// List of IP address (in human readable format).
typedef std::list<std::string> IpList;
// Map from hostname to IpList.
typedef std::unordered_map<std::string, IpList> HostMap;
// List of server.
typedef std::list<std::pair<std::string, uint16_t>> HostList;
// Map from service to HostList.
typedef std::unordered_map<std::string, HostList> ServiceMap;
// Represents a single TestDnsServer query state and lifecycle. This implements
// just enough of RFC 1035 to handle queries we generate in the tests below.
class TestDnsServerQuery {
public:
  TestDnsServerQuery(ConnectionPtr connection, const HostMap& hosts, const ServiceMap& service)
      : connection_(std::move(connection)), hosts_(hosts), services_(service) {
    connection_->addReadFilter(Network::ReadFilterPtr{new ReadFilter(*this)});
  }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(TestDnsServerQuery& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
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
          size_n = *static_cast<uint16_t*>(buffer_.linearize(sizeof(size_n)));
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
        long question_len;
        char* name;
        ASSERT_EQ(ARES_SUCCESS, ares_expand_name(question, request, size_, &name, &question_len));

        const std::list<std::string>* ips = nullptr;
        const std::list<std::pair<std::string, uint16_t>>* hosts = nullptr;
        uint16_t answer_count = 0;
        if (DNS_QUESTION_TYPE(question + question_len) == T_A) {
          auto it = parent_.hosts_.find(name);
          if (it != parent_.hosts_.end()) {
            ips = &it->second;
            answer_count = ips->size();
          }
        } else if (DNS_QUESTION_TYPE(question + question_len) == T_SRV) {
          auto it = parent_.services_.find(name);
          if (it != parent_.services_.end()) {
            hosts = &it->second;
            answer_count = hosts->size();
          }
        }
        ares_free_string(name);

        // The response begins with the intial part of the request
        // (including the question section).
        const size_t response_base_len = HFIXEDSZ + question_len + QFIXEDSZ;
        unsigned char response_base[response_base_len];
        memcpy(response_base, request, response_base_len);
        DNS_HEADER_SET_QR(response_base, 1);
        DNS_HEADER_SET_AA(response_base, 0);
        DNS_HEADER_SET_RCODE(response_base, answer_count != 0 ? NOERROR : NXDOMAIN);
        DNS_HEADER_SET_ANCOUNT(response_base, answer_count);
        DNS_HEADER_SET_NSCOUNT(response_base, 0);
        DNS_HEADER_SET_ARCOUNT(response_base, 0);

        // An A resource record for each IP found in the host map.

        unsigned char response_rr_fixed[RRFIXEDSZ];
        DNS_RR_SET_TYPE(response_rr_fixed, DNS_QUESTION_TYPE(question + question_len));
        DNS_RR_SET_CLASS(response_rr_fixed, C_IN);
        DNS_RR_SET_TTL(response_rr_fixed, 0);

        size_t response_len = response_base_len;
        if (DNS_QUESTION_TYPE(question + question_len) == T_A) {
          response_len += answer_count * (question_len + RRFIXEDSZ + sizeof(in_addr));
          DNS_RR_SET_LEN(response_rr_fixed, sizeof(in_addr));
        } else if (DNS_QUESTION_TYPE(question + question_len) == T_SRV) {
          // Calculation SRV struct length
          size_t srv_len = 0;
          if (hosts != nullptr) {
            for (auto it : *hosts) {
              srv_len += it.first.size() + 1 /* uint8_t len for first label */ +
                         1 /* '\0' for last label */ + 2 /* uint16_t priority */ +
                         2 /* uint16_t weight */ + 2 /* uint16_t port */;
            }
          }
          response_len += answer_count * (question_len + RRFIXEDSZ) + srv_len;
        }
        const uint16_t response_size_n = htons(response_len);
        Buffer::OwnedImpl write_buffer_;
        write_buffer_.add(&response_size_n, sizeof(response_size_n));
        write_buffer_.add(response_base, response_base_len);
        if (DNS_QUESTION_TYPE(question + question_len) == T_A) {
          if (ips != nullptr) {
            for (auto it : *ips) {
              write_buffer_.add(question, question_len);
              write_buffer_.add(response_rr_fixed, RRFIXEDSZ);
              in_addr addr;
              ASSERT_EQ(1, inet_pton(AF_INET, it.c_str(), &addr));
              write_buffer_.add(&addr, sizeof(addr));
            }
          }
        } else if (DNS_QUESTION_TYPE(question + question_len) == T_SRV) {
          if (hosts != nullptr) {
            for (auto it : *hosts) {
              write_buffer_.add(question, question_len);
              DNS_RR_SET_LEN(response_rr_fixed,
                             it.first.size() + 1 /* uint8_t len for first label */ +
                                 1 /* '\0' for last label */ + 2 /* uint16_t priority */ +
                                 2 /* uint16_t weight */ + 2 /* uint16_t port */);
              write_buffer_.add(response_rr_fixed, RRFIXEDSZ);
              uint16_t priority = 0;
              write_buffer_.add(&priority, sizeof(priority));
              uint16_t weight = 0;
              write_buffer_.add(&weight, sizeof(weight));
              uint16_t port = htons(it.second);
              write_buffer_.add(&port, sizeof(port));
              uint8_t len;
              for (uint16_t i = 0; i < it.first.size(); /**/) {
                size_t pos = it.first.find('.', i);
                if (pos == it.first.npos) {
                  pos = it.first.size();
                }
                len = pos - i;
                write_buffer_.add(&len, sizeof(len));
                write_buffer_.add(&it.first[i], len);
                i = pos + 1;
              }
              const char root_label = '\0';
              write_buffer_.add(&root_label, 1);
            }
          }
        }

        // Send response to client.
        parent_.connection_->write(write_buffer_);

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
  const HostMap& hosts_;
  const ServiceMap& services_;
};

class TestDnsServer : public ListenerCallbacks {
public:
  void onNewConnection(ConnectionPtr&& new_connection) override {
    TestDnsServerQuery* query =
        new TestDnsServerQuery(std::move(new_connection), hosts_, services_);
    queries_.emplace_back(query);
  }

  void addHosts(const std::string& hostname, const IpList& ip) { hosts_[hostname] = ip; }
  void addServices(const std::string& service, const HostList& host) { services_[service] = host; }

private:
  HostMap hosts_;
  ServiceMap services_;
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

class DnsImplTest : public testing::Test {
public:
  void SetUp() override {
    resolver_ = dispatcher_.createDnsResolver();

    // Point c-ares at 127.0.0.1:10000 with no search domains and TCP-only.
    peer_.reset(new DnsResolverImplPeer(dynamic_cast<DnsResolverImpl*>(resolver_.get())));
    peer_->resetChannelTcpOnly(zero_timeout());
    ares_set_servers_ports_csv(peer_->channel(), "127.0.0.1:10000");

    // Instantiate TestDnsServer and listen on 127.0.0.1:10000.
    server_.reset(new TestDnsServer());
    socket_.reset(new Network::TcpListenSocket(uint32_t(10000), true));
    listener_ = dispatcher_.createListener(connection_handler_, *socket_, *server_, stats_store_,
                                           true, false, false);
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
  DnsResolverPtr resolver_;
};

static bool hasAddress(const std::list<Address::InstancePtr>& results, const std::string& address) {
  for (auto result : results) {
    if (result->asString() == address) {
      return true;
    }
  }
  return false;
}

// Validate that when DnsResolverImpl is destructed with outstanding requests,
// that we don't invoke any callbacks. This is a regression test from
// development, where segfaults were encountered due to callback invocations on
// destruction.
TEST_F(DnsImplTest, DestructPending) {
  EXPECT_NE(nullptr,
            resolver_->resolve("", 80, [&](std::list<Address::InstancePtr>&& results) -> void {
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
TEST_F(DnsImplTest, LocalLookup) {
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr,
            resolver_->resolve("", 80, [&](std::list<Address::InstancePtr>&& results) -> void {
              address_list = results;
              dispatcher_.exit();
            }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  EXPECT_EQ(nullptr,
            resolver_->resolve("localhost", 80, [&](std::list<Address::InstancePtr>&& results)
                                                    -> void { address_list = results; }));
  EXPECT_TRUE(hasAddress(address_list, "127.0.0.1:80"));
}

// Validate success/fail lookup behavior via TestDnsServer. This exercises the
// network event handling in DnsResolverImpl.
TEST_F(DnsImplTest, RemoteAsyncLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.bad.domain", 80,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain", 80,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:80"));

  // Test SRV lookup
  server_->addServices(
      "test.service.consul",
      {{"some.good.domain", 8080}, {"some.good.domain", 8081}, {"some.good.domain", 8082}});
  EXPECT_NE(nullptr, resolver_->resolve("test.service.consul", 0,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:8080"));
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:8081"));
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:8082"));
}

// Validate that multiple A records are correctly passed to the callback.
TEST_F(DnsImplTest, MultiARecordLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain", 80,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:80"));
  EXPECT_TRUE(hasAddress(address_list, "123.4.5.6:80"));
  EXPECT_TRUE(hasAddress(address_list, "6.5.4.3:80"));
}

// Validate that multiple SRV records are correctly passed to the callback.
TEST_F(DnsImplTest, MultiSRVRecordLookup) {
  server_->addHosts("s1.good.domain", {"201.134.56.7", "123.4.5.6"});
  server_->addHosts("s2.good.domain", {"6.5.4.4"});
  server_->addServices("test.service.consul", {{"s1.good.domain", 8080}, {"s2.good.domain", 8081}});
  std::list<Address::InstancePtr> address_srv_list;
  EXPECT_NE(nullptr, resolver_->resolve("test.service.consul", 0,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_srv_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_srv_list, "201.134.56.7:8080"));
  EXPECT_TRUE(hasAddress(address_srv_list, "123.4.5.6:8080"));
  EXPECT_TRUE(hasAddress(address_srv_list, "6.5.4.4:8081"));
}

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_F(DnsImplTest, Cancel) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});

  ActiveDnsQuery* query = resolver_->resolve(
      "some.domain", 80, [](std::list<Address::InstancePtr> && ) -> void { FAIL(); });

  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain", 80,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  ASSERT_NE(nullptr, query);
  query->cancel();

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7:80"));
}

class DnsImplZeroTimeoutTest : public DnsImplTest {
protected:
  bool zero_timeout() const override { return true; }
};

// Validate that timeouts result in an empty callback.
TEST_F(DnsImplZeroTimeoutTest, Timeout) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain", 80,
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_.exit();
                                        }));

  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());
}

} // Network
