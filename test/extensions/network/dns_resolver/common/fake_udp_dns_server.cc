#include "test/extensions/network/dns_resolver/common/fake_udp_dns_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {
namespace Test {

FakeUdpDnsServer::FakeUdpDnsServer(bool ipv6) {
  const int family = ipv6 ? AF_INET6 : AF_INET;
  fd_ = socket(family, SOCK_DGRAM, 0);
  RELEASE_ASSERT(fd_ >= 0, "Failed to create UDP socket for FakeUdpDnsServer.");

  // Set non-blocking.
  const int flags = fcntl(fd_, F_GETFL, 0);
  RELEASE_ASSERT(flags >= 0, "fcntl F_GETFL failed.");
  RELEASE_ASSERT(fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0, "fcntl F_SETFL failed.");

  if (ipv6) {
    struct sockaddr_in6 addr {};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_loopback;
    RELEASE_ASSERT(bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0,
                   "Failed to bind IPv6 UDP socket.");

    struct sockaddr_in6 bound {};
    socklen_t len = sizeof(bound);
    getsockname(fd_, reinterpret_cast<struct sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin6_port);
    address_ = "::1";
  } else {
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    RELEASE_ASSERT(bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0,
                   "Failed to bind IPv4 UDP socket.");

    struct sockaddr_in bound {};
    socklen_t len = sizeof(bound);
    getsockname(fd_, reinterpret_cast<struct sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);
    address_ = "127.0.0.1";
  }
}

FakeUdpDnsServer::~FakeUdpDnsServer() {
  stop();
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

void FakeUdpDnsServer::setDefaultAResponse(const std::string& ipv4_address, uint32_t ttl) {
  default_a_ = {ipv4_address, ttl, true};
}

void FakeUdpDnsServer::setDefaultAAAAResponse(const std::string& ipv6_address, uint32_t ttl) {
  default_aaaa_ = {ipv6_address, ttl, true};
}

void FakeUdpDnsServer::start() {
  RELEASE_ASSERT(!running_.load(), "FakeUdpDnsServer already running.");
  running_.store(true);
  thread_ = std::thread([this] { serve(); });
}

void FakeUdpDnsServer::stop() {
  if (running_.exchange(false)) {
    thread_.join();
  }
}

void FakeUdpDnsServer::serve() {
  uint8_t buf[512];
  struct sockaddr_storage client_addr {};
  struct pollfd pfd {};
  pfd.fd = fd_;
  pfd.events = POLLIN;

  while (running_.load(std::memory_order_relaxed)) {
    const int poll_ret = poll(&pfd, 1, /*timeout_ms=*/1);
    if (poll_ret <= 0) {
      continue;
    }

    socklen_t client_len = sizeof(client_addr);
    const ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0,
                               reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    if (n <= 0) {
      continue;
    }

    queries_received_.fetch_add(1, std::memory_order_relaxed);
    const auto response = buildResponse(buf, static_cast<size_t>(n));
    if (!response.empty()) {
      sendto(fd_, response.data(), response.size(), 0,
             reinterpret_cast<const struct sockaddr*>(&client_addr), client_len);
      responses_sent_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

std::string FakeUdpDnsServer::parseDnsName(const uint8_t* data, size_t len, size_t& offset) {
  std::string name;
  while (offset < len) {
    const uint8_t label_len = data[offset++];
    if (label_len == 0) {
      break;
    }
    if ((label_len & 0xC0) == 0xC0) {
      // Compression pointer — skip the second byte and stop.
      if (offset < len) {
        offset++;
      }
      break;
    }
    if (offset + label_len > len) {
      break;
    }
    if (!name.empty()) {
      name += ".";
    }
    name.append(reinterpret_cast<const char*>(data + offset), label_len);
    offset += label_len;
  }
  return name;
}

std::vector<uint8_t> FakeUdpDnsServer::buildResponse(const uint8_t* query, size_t query_len) const {
  static constexpr size_t kDnsHeaderSize = 12;
  if (query_len < kDnsHeaderSize) {
    return {};
  }

  // Parse the question by skipping the header, read name + `QTYPE` + `QCLASS`.
  size_t offset = kDnsHeaderSize;
  parseDnsName(query, query_len, offset);
  if (offset + 4 > query_len) {
    return {};
  }
  const uint16_t qtype =
      (static_cast<uint16_t>(query[offset]) << 8) | static_cast<uint16_t>(query[offset + 1]);
  const size_t question_end = offset + 4; // `QTYPE`(2) + `QCLASS`(2).

  // Select response data.
  const DefaultRecord* record = nullptr;
  uint16_t rdlength = 0;
  if (qtype == kDnsTypeA && default_a_.enabled) {
    record = &default_a_;
    rdlength = 4;
  } else if (qtype == kDnsTypeAAAA && default_aaaa_.enabled) {
    record = &default_aaaa_;
    rdlength = 16;
  }

  const bool has_answer = (record != nullptr);

  std::vector<uint8_t> resp;
  resp.reserve(question_end + (has_answer ? 16 + rdlength : 0));

  // Header: copy ID, set response flags.
  resp.push_back(query[0]);
  resp.push_back(query[1]);
  // Byte 2: QR=1 OPCODE=0000 AA=1 TC=0 RD=1 → 0x85.
  resp.push_back(0x85);
  // Byte 3: RA=1 Z=000 `RCODE`: 0 (`NOERROR`) or 3 (`NXDOMAIN`).
  resp.push_back(has_answer ? static_cast<uint8_t>(0x80) : static_cast<uint8_t>(0x83));

  // QDCOUNT=1.
  resp.push_back(0x00);
  resp.push_back(0x01);
  // `ANCOUNT`.
  resp.push_back(0x00);
  resp.push_back(has_answer ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00));
  // `NSCOUNT`=0, `ARCOUNT`=0.
  resp.push_back(0x00);
  resp.push_back(0x00);
  resp.push_back(0x00);
  resp.push_back(0x00);

  // Echo the question section.
  resp.insert(resp.end(), query + kDnsHeaderSize, query + question_end);

  if (has_answer) {
    // Name: pointer to question name at offset 0x0C.
    resp.push_back(0xC0);
    resp.push_back(0x0C);

    // TYPE.
    resp.push_back(static_cast<uint8_t>(qtype >> 8));
    resp.push_back(static_cast<uint8_t>(qtype & 0xFF));

    // CLASS = IN (1).
    resp.push_back(0x00);
    resp.push_back(0x01);

    // TTL (network byte order).
    const uint32_t ttl = record->ttl;
    resp.push_back(static_cast<uint8_t>((ttl >> 24) & 0xFF));
    resp.push_back(static_cast<uint8_t>((ttl >> 16) & 0xFF));
    resp.push_back(static_cast<uint8_t>((ttl >> 8) & 0xFF));
    resp.push_back(static_cast<uint8_t>(ttl & 0xFF));

    // `RDLENGTH`.
    resp.push_back(static_cast<uint8_t>(rdlength >> 8));
    resp.push_back(static_cast<uint8_t>(rdlength & 0xFF));

    // `RDATA`.
    if (qtype == kDnsTypeA) {
      struct in_addr addr {};
      inet_pton(AF_INET, record->address.c_str(), &addr);
      const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
      resp.insert(resp.end(), bytes, bytes + 4);
    } else {
      struct in6_addr addr6 {};
      inet_pton(AF_INET6, record->address.c_str(), &addr6);
      const auto* bytes = reinterpret_cast<const uint8_t*>(&addr6);
      resp.insert(resp.end(), bytes, bytes + 16);
    }
  }

  return resp;
}

} // namespace Test
} // namespace Network
} // namespace Envoy
