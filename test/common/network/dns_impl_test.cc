#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "common/api/api_impl.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/dns_impl.h"

#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>

#include "ares.h"
#include "ares_dns.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/event.h"
#include "event2/listener.h"

namespace Network {

namespace {

// List of IP address (in human readable format).
typedef std::list<std::string> IpList;
// Map from hostname to IpList.
typedef std::unordered_map<std::string, IpList> HostMap;

// Represents a single TestDnsServer query state and lifecycle. This implements
// just enough of RFC 1035 to handle queries we generate in the tests below.
class TestDnsServerQuery {
public:
  TestDnsServerQuery(struct bufferevent* bev, const HostMap& hosts)
      : bufferevent_(bev), hosts_(hosts) {}
  ~TestDnsServerQuery() { bufferevent_free(bufferevent_); }

  void onRead(struct bufferevent* bev) {
    while (true) {
      struct evbuffer* input = bufferevent_get_input(bev);
      size_t len = evbuffer_get_length(input);
      if (size_ == 0) {
        uint16_t size_n;
        if (len < sizeof(size_n)) {
          // If we don't have enough bytes to determine size, wait until we do.
          bufferevent_setwatermark(bev, EV_READ, sizeof(size_n), 0);
          return;
        }
        ASSERT_EQ(sizeof(size_n), bufferevent_read(bev, &size_n, sizeof(size_n)));
        size_ = ntohs(size_n);
        len -= sizeof(size_n);
      }

      if (len < size_) {
        // If we don't have enough bytes to read the complete query, wait until
        // we do.
        bufferevent_setwatermark(bev, EV_READ, size_, 0);
        return;
      }

      // Expect requests to be small, so stack allocation is fine for test code.
      unsigned char request[size_];
      ASSERT_EQ(size_, bufferevent_read(bev, request, size_));
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
      auto it = hosts_.find(name);
      const std::list<std::string>* ips = nullptr;
      if (it != hosts_.end()) {
        ips = &it->second;
      }
      ares_free_string(name);

      // The response begins with the intial part of the request
      // (including the question section).
      const size_t response_base_len = HFIXEDSZ + question_len + QFIXEDSZ;
      unsigned char response_base[response_base_len];
      memcpy(response_base, request, response_base_len);
      DNS_HEADER_SET_QR(response_base, 1);
      DNS_HEADER_SET_AA(response_base, 0);
      DNS_HEADER_SET_RCODE(response_base, ips != nullptr ? NOERROR : NXDOMAIN);
      DNS_HEADER_SET_ANCOUNT(response_base, ips != nullptr ? ips->size() : 0);
      DNS_HEADER_SET_NSCOUNT(response_base, 0);
      DNS_HEADER_SET_ARCOUNT(response_base, 0);

      // An A resource record for each IP found in the host map.
      const size_t response_rest_len =
          ips != nullptr ? ips->size() * (question_len + RRFIXEDSZ + sizeof(struct in_addr)) : 0;
      unsigned char response_rr_fixed[RRFIXEDSZ];
      DNS_RR_SET_TYPE(response_rr_fixed, T_A);
      DNS_RR_SET_CLASS(response_rr_fixed, C_IN);
      DNS_RR_SET_TTL(response_rr_fixed, 0);
      DNS_RR_SET_LEN(response_rr_fixed, sizeof(struct in_addr));

      // Send response to client.
      const uint16_t response_size_n = htons(response_base_len + response_rest_len);
      ASSERT_EQ(0, bufferevent_write(bev, &response_size_n, sizeof(response_size_n)));
      ASSERT_EQ(0, bufferevent_write(bev, response_base, response_base_len));
      if (ips != nullptr) {
        for (auto it : *ips) {
          ASSERT_EQ(0, bufferevent_write(bev, question, question_len));
          ASSERT_EQ(0, bufferevent_write(bev, response_rr_fixed, 10));
          struct in_addr addr;
          ASSERT_EQ(1, inet_pton(AF_INET, it.c_str(), &addr));
          ASSERT_EQ(0, bufferevent_write(bev, &addr, sizeof(addr)));
        }
      }

      // Reset query state, time for the next one.
      size_ = 0;
    }
  }

  void onError(struct bufferevent* bev, short events) {
    UNREFERENCED_PARAMETER(bev);
    EXPECT_TRUE(!(events & BEV_EVENT_ERROR));
  }

private:
  struct bufferevent* bufferevent_;
  const HostMap& hosts_;
  // The expected size of the current DNS query to read. If zero, indicates that
  // no DNS query is in progress and that a 2 byte size is expected from the
  // client to indicate the next DNS query size.
  uint16_t size_ = 0;
};

class TestDnsServer {
public:
  TestDnsServer(struct event_base* base) : event_base_(base) {}

  void onAccept(int fd) {
    struct bufferevent* bev = bufferevent_socket_new(event_base_, fd, BEV_OPT_CLOSE_ON_FREE);
    TestDnsServerQuery* query = new TestDnsServerQuery(bev, hosts_);
    queries_.emplace_back(query);
    bufferevent_setcb(bev, [](struct bufferevent* bev,
                              void* ctx) { static_cast<TestDnsServerQuery*>(ctx)->onRead(bev); },
                      nullptr,
                      [](struct bufferevent* bev, short error, void* ctx) {
                        static_cast<TestDnsServerQuery*>(ctx)->onError(bev, error);
                      },
                      query);
    bufferevent_setwatermark(bev, EV_READ, 2, 0);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
  }

  void addHosts(const std::string& hostname, const IpList& ip) { hosts_[hostname] = ip; }

private:
  HostMap hosts_;
  struct event_base* event_base_;
  // All queries are tracked so we can do resource reclamation when the test is
  // over.
  std::vector<std::unique_ptr<TestDnsServerQuery>> queries_;
};
} // namespace

class DnsResolverImplPeer {
public:
  DnsResolverImplPeer(DnsResolverImpl* resolver) : resolver_(resolver) {}
  ares_channel channel() const { return resolver_->channel_; }
  const std::unordered_map<int, event*>& events() { return resolver_->events_; }
  // Reset the channel state for a DnsResolverImpl such that it will only use
  // TCP and optionally has a zero timeout (for validating timeout behavior).
  void resetChannelTcpOnly(bool zero_timeout) {
    ares_destroy(resolver_->channel_);
    struct ares_options options;
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
    Api::Impl api(std::chrono::milliseconds(10000));
    dispatcher_ = api.allocateDispatcher();
    resolver_ = dispatcher_->createDnsResolver();

    // Point c-ares at 127.0.0.1:10000 with no search domains and TCP-only.
    peer_.reset(new DnsResolverImplPeer(dynamic_cast<DnsResolverImpl*>(resolver_.get())));
    peer_->resetChannelTcpOnly(zero_timeout());
    ares_set_servers_ports_csv(peer_->channel(), "127.0.0.1:10000");

    // Instantiate TestDnsServer and listen on 127.0.0.1:10000.
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(10000);
    struct event_base* base = &dynamic_cast<Event::DispatcherImpl*>(dispatcher_.get())->base();
    server_.reset(new TestDnsServer(base));
    listener_ = evconnlistener_new_bind(base,
                                        [](struct evconnlistener* listener, evutil_socket_t fd,
                                           struct sockaddr* address, int socklen, void* arg) {
                                          static_cast<TestDnsServer*>(arg)->onAccept(fd);
                                          UNREFERENCED_PARAMETER(listener);
                                          UNREFERENCED_PARAMETER(address);
                                          UNREFERENCED_PARAMETER(socklen);
                                        },
                                        server_.get(), LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
                                        -1, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
    ASSERT_NE(nullptr, listener_);
  }
  void TearDown() override { evconnlistener_free(listener_); }

protected:
  // Should the DnsResolverImpl use a zero timeout for c-ares queries?
  virtual bool zero_timeout() const { return false; }
  struct evconnlistener* listener_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverPtr resolver_;
  std::unique_ptr<DnsResolverImplPeer> peer_;
  std::unique_ptr<TestDnsServer> server_;
};

static bool hasAddress(const std::list<Address::InstancePtr>& results, const std::string& address) {
  for (auto result : results) {
    if (result->ip()->addressAsString() == address) {
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
  EXPECT_NE(nullptr, resolver_->resolve("", [&](std::list<Address::InstancePtr>&& results) -> void {
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
  EXPECT_NE(nullptr, resolver_->resolve("", [&](std::list<Address::InstancePtr>&& results) -> void {
    address_list = results;
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  EXPECT_EQ(nullptr, resolver_->resolve("localhost", [&](std::list<Address::InstancePtr>&& results)
                                                         -> void { address_list = results; }));
  EXPECT_TRUE(hasAddress(address_list, "127.0.0.1"));
}

// Validate success/fail lookup behavior via TestDnsServer. This exercises the
// network event handling in DnsResolverImpl.
TEST_F(DnsImplTest, RemoteAsyncLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.bad.domain",
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_->exit();
                                        }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain",
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_->exit();
                                        }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
}

// Validate that multiple A records are correctly passed to the callback.
TEST_F(DnsImplTest, MultiARecordLookup) {
  server_->addHosts("some.good.domain", {"201.134.56.7", "123.4.5.6", "6.5.4.3"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain",
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_->exit();
                                        }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
  EXPECT_TRUE(hasAddress(address_list, "123.4.5.6"));
  EXPECT_TRUE(hasAddress(address_list, "6.5.4.3"));
}

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_F(DnsImplTest, Cancel) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});

  ActiveDnsQuery* query = resolver_->resolve("some.domain", [](std::list<Address::InstancePtr> && )
                                                                -> void { FAIL(); });

  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain",
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_->exit();
                                        }));

  ASSERT_NE(nullptr, query);
  query->cancel();

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "201.134.56.7"));
}

class DnsImplZeroTimeoutTest : public DnsImplTest {
protected:
  bool zero_timeout() const override { return true; }
};

// Validate that timeouts result in an empty callback.
TEST_F(DnsImplZeroTimeoutTest, Timeout) {
  server_->addHosts("some.good.domain", {"201.134.56.7"});
  std::list<Address::InstancePtr> address_list;
  EXPECT_NE(nullptr, resolver_->resolve("some.good.domain",
                                        [&](std::list<Address::InstancePtr>&& results) -> void {
                                          address_list = results;
                                          dispatcher_->exit();
                                        }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());
}

} // Network
