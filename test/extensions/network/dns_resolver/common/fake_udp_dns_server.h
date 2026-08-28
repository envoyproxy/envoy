#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/network/socket.h"

namespace Envoy {
namespace Network {
namespace Test {

// Minimal UDP DNS server for benchmarking and testing DNS resolvers.
// Responds to A/AAAA queries with configurable default addresses on loopback.
//
// All of the server's I/O is driven by the dispatcher handed to the constructor:
// queries are read and responses written from a read event, so the server only
// makes progress while that dispatcher is running. Passing the same dispatcher
// the resolver under test uses keeps both sides of the exchange on one event
// loop and one thread.
class FakeUdpDnsServer {
public:
  static constexpr uint16_t kDnsTypeA = 1;
  static constexpr uint16_t kDnsTypeAAAA = 28;

  // Binds a UDP socket to loopback on a random port and starts listening for
  // queries on `dispatcher`. Use ipv6=true for [::1].
  explicit FakeUdpDnsServer(Event::Dispatcher& dispatcher, bool ipv6 = false);
  virtual ~FakeUdpDnsServer();

  // Not copyable or movable.
  FakeUdpDnsServer(const FakeUdpDnsServer&) = delete;
  FakeUdpDnsServer& operator=(const FakeUdpDnsServer&) = delete;

  // Set the default A record returned for any A query.
  void setDefaultAResponse(const std::string& ipv4_address, uint32_t ttl = 300);

  // Set the default AAAA record returned for any AAAA query.
  void setDefaultAAAAResponse(const std::string& ipv6_address, uint32_t ttl = 300);

  // Bound port (available immediately after construction).
  uint16_t port() const { return port_; }

  // Loopback address string matching the socket family.
  const std::string& address() const { return address_; }

  // Counters. Updated on, and so only safe to read from, the dispatcher's thread.
  uint64_t queriesReceived() const { return queries_received_; }
  uint64_t responsesSent() const { return responses_sent_; }

protected:
  // Builds up to 2 responses. Empty responses will be skipped.
  virtual std::array<std::vector<uint8_t>, 2> makeResponses(const uint8_t* query, size_t query_len);

  // Build a DNS response for the given query. Returns empty vector on parse failure.
  std::vector<uint8_t> buildResponse(const uint8_t* query, size_t query_len) const;

  // Build a NOERROR/NODATA response returned for an IPv4-only to AAAA query. Returns empty vector
  // on parse failure.
  std::vector<uint8_t> buildNoDataResponse(const uint8_t* query, size_t query_len) const;

private:
  // Read event handler: answers every datagram queued on the socket.
  void onReadReady();
  // Write event handler: sends outgoing queued datagrams.
  void tryFlushOutgoing();

  struct DefaultRecord {
    std::string address;
    uint32_t ttl{300};
    bool enabled{false};
  };

  SocketPtr socket_;
  uint16_t port_{0};
  std::string address_;
  std::deque<std::pair<std::vector<uint8_t>, Address::InstanceConstSharedPtr>> outgoing_;

  uint64_t queries_received_{0};
  uint64_t responses_sent_{0};

  DefaultRecord default_a_;
  DefaultRecord default_aaaa_;
};

} // namespace Test
} // namespace Network
} // namespace Envoy
