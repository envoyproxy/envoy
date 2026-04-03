#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace Envoy {
namespace Network {
namespace Test {

// Minimal threaded UDP DNS server for benchmarking and testing DNS resolvers.
// Responds to A/AAAA queries with configurable default addresses on loopback.
// The server runs in a background thread and processes queries sequentially
// via non-blocking I/O with poll().
class FakeUdpDnsServer {
public:
  static constexpr uint16_t kDnsTypeA = 1;
  static constexpr uint16_t kDnsTypeAAAA = 28;

  // Binds a UDP socket to loopback on a random port. Use ipv6=true for [::1].
  explicit FakeUdpDnsServer(bool ipv6 = false);
  ~FakeUdpDnsServer();

  // Not copyable or movable.
  FakeUdpDnsServer(const FakeUdpDnsServer&) = delete;
  FakeUdpDnsServer& operator=(const FakeUdpDnsServer&) = delete;

  // Set the default A record returned for any A query.
  void setDefaultAResponse(const std::string& ipv4_address, uint32_t ttl = 300);

  // Set the default AAAA record returned for any AAAA query.
  void setDefaultAAAAResponse(const std::string& ipv6_address, uint32_t ttl = 300);

  // Start/stop the server thread.
  void start();
  void stop();

  // Bound port (available immediately after construction).
  uint16_t port() const { return port_; }

  // Loopback address string matching the socket family.
  const std::string& address() const { return address_; }

  // Counters (thread-safe, relaxed ordering).
  uint64_t queriesReceived() const { return queries_received_.load(std::memory_order_relaxed); }
  uint64_t responsesSent() const { return responses_sent_.load(std::memory_order_relaxed); }

private:
  // Background thread entry point.
  void serve();

  // Parse a DNS label-encoded name starting at offset. Advances offset past the name.
  static std::string parseDnsName(const uint8_t* data, size_t len, size_t& offset);

  // Build a DNS response for the given query. Returns empty vector on parse failure.
  std::vector<uint8_t> buildResponse(const uint8_t* query, size_t query_len) const;

  struct DefaultRecord {
    std::string address;
    uint32_t ttl{300};
    bool enabled{false};
  };

  int fd_{-1};
  uint16_t port_{0};
  std::string address_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> queries_received_{0};
  std::atomic<uint64_t> responses_sent_{0};

  DefaultRecord default_a_;
  DefaultRecord default_aaaa_;
};

} // namespace Test
} // namespace Network
} // namespace Envoy
