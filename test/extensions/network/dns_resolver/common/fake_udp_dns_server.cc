#include "test/extensions/network/dns_resolver/common/fake_udp_dns_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>
#include <optional>

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"

#include "source/common/common/assert.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Test {
namespace {
// Maximum size of a DNS message over UDP without EDNS(0) (RFC 1035).
static constexpr size_t kMaxDnsMessageSize = 512;

// Parse a DNS label-encoded name starting at offset. Advances offset past the name.
static std::string parseDnsName(const uint8_t* data, size_t len, size_t& offset) {
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

static constexpr size_t kDnsHeaderSize = 12;

struct ParsedQuestion {
  uint16_t qtype{0};
  // Offset just past the end of the question section.
  size_t question_end{0};
};

std::optional<ParsedQuestion> parseQuestion(const uint8_t* query, size_t query_len) {
  if (query_len < kDnsHeaderSize) {
    return std::nullopt;
  }

  size_t offset = kDnsHeaderSize;
  parseDnsName(query, query_len, offset);
  if (offset + 4 > query_len) {
    return std::nullopt;
  }

  return ParsedQuestion{
      .qtype = static_cast<uint16_t>((static_cast<uint16_t>(query[offset]) << 8) |
                                     static_cast<uint16_t>(query[offset + 1])),
      .question_end = offset + 4, // `QTYPE`(2) + `QCLASS`(2).
  };
}
} // namespace

FakeUdpDnsServer::FakeUdpDnsServer(Event::Dispatcher& dispatcher, bool ipv6) {
  const auto loopback =
      ipv6 ? Utility::getIpv6LoopbackAddress() : Utility::getCanonicalIpv4LoopbackAddress();
  socket_ = std::make_unique<UdpListenSocket>(loopback, /*options=*/nullptr,
                                              /*bind_to_port=*/true);

  const Address::Instance& bound = *socket_->connectionInfoProvider().localAddress();
  port_ = bound.ip()->port();
  address_ = bound.ip()->addressAsString();

  socket_->ioHandle().initializeFileEvent(
      dispatcher,
      [this](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          onReadReady();
        }
        if (events & Event::FileReadyType::Write) {
          tryFlushOutgoing();
        }
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read | Event::FileReadyType::Write);
}

FakeUdpDnsServer::~FakeUdpDnsServer() { socket_->ioHandle().resetFileEvents(); }

void FakeUdpDnsServer::setDefaultAResponse(const std::string& ipv4_address, uint32_t ttl) {
  default_a_ = {ipv4_address, ttl, true};
}

void FakeUdpDnsServer::setDefaultAAAAResponse(const std::string& ipv6_address, uint32_t ttl) {
  default_aaaa_ = {ipv6_address, ttl, true};
}

void FakeUdpDnsServer::onReadReady() {
  IoHandle& io_handle = socket_->ioHandle();
  const IoHandle::UdpSaveCmsgConfig save_cmsg_config;
  uint32_t dropped_packets = 0;
  uint8_t buf[kMaxDnsMessageSize];

  // The event is edge triggered on most platforms, so keep reading until the
  // socket queue is drained rather than handling a single query per event.
  while (true) {
    Buffer::RawSlice slice{buf, sizeof(buf)};
    IoHandle::RecvMsgOutput output(/*num_packets_per_call=*/1, &dropped_packets);
    const Api::IoCallUint64Result result =
        io_handle.recvmsg(&slice, 1, port_, save_cmsg_config, output);
    if (!result.ok()) {
      // `Again` means the queue is empty. Any other error is not something a
      // fake server can recover from, and the next event will retry anyway.
      tryFlushOutgoing();
      return;
    }
    if (result.return_value_ == 0) {
      // Empty or truncated datagram; `recvmsg` consumed it, so keep draining.
      continue;
    }

    queries_received_++;
    auto responses = makeResponses(buf, result.return_value_);

    for (auto& response : responses) {
      if (response.empty()) {
        continue;
      }

      outgoing_.emplace_back(response, output.msg_[0].peer_address_);

      // Try writing any outgoing messages to the socket.
      tryFlushOutgoing();
    }
  }
}

void FakeUdpDnsServer::tryFlushOutgoing() {
  IoHandle& io_handle = socket_->ioHandle();

  while (!outgoing_.empty()) {
    auto& next = outgoing_.front();

    Buffer::RawSlice response_slice{next.first.data(), next.first.size()};
    const Api::IoCallUint64Result send_result =
        io_handle.sendmsg(&response_slice, 1, /*flags=*/0, /*self_ip=*/nullptr, *next.second);
    if (send_result.wouldBlock()) {
      return;
    }
    // Either the send succeeded or failed with an error we can't just retry on
    // later. Either way remove the outgoing message from the queue.
    outgoing_.pop_front();
  }
}

std::array<std::vector<std::uint8_t>, 2> FakeUdpDnsServer::makeResponses(const uint8_t* query,
                                                                         size_t query_len) {
  return {buildResponse(query, query_len), std::vector<std::uint8_t>{}};
}

std::vector<uint8_t> FakeUdpDnsServer::buildResponse(const uint8_t* query, size_t query_len) const {
  const auto question = parseQuestion(query, query_len);
  if (!question.has_value()) {
    return {};
  }
  const uint16_t qtype = question->qtype;
  const size_t question_end = question->question_end;

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
      struct in_addr addr{};
      inet_pton(AF_INET, record->address.c_str(), &addr);
      const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
      resp.insert(resp.end(), bytes, bytes + 4);
    } else {
      struct in6_addr addr6{};
      inet_pton(AF_INET6, record->address.c_str(), &addr6);
      const auto* bytes = reinterpret_cast<const uint8_t*>(&addr6);
      resp.insert(resp.end(), bytes, bytes + 16);
    }
  }

  return resp;
}

std::vector<uint8_t> FakeUdpDnsServer::buildNoDataResponse(const uint8_t* query,
                                                           size_t query_len) const {
  const auto question = parseQuestion(query, query_len);
  if (!question.has_value()) {
    return {};
  }
  const size_t question_end = question->question_end;

  std::vector<uint8_t> resp;
  resp.reserve(question_end);

  // Header: copy ID, set response flags.
  resp.push_back(query[0]);
  resp.push_back(query[1]);
  // Byte 2: QR=1 OPCODE=0000 AA=1 TC=0 RD=1 -> 0x85.
  resp.push_back(0x85);
  // Byte 3: RA=1 Z=000 `RCODE`=0 (`NOERROR`).
  resp.push_back(0x80);

  // QDCOUNT=1. `ANCOUNT`=0 alongside `NOERROR` is what makes this `NODATA` and not `NXDOMAIN`.
  resp.push_back(0x00);
  resp.push_back(0x01);
  resp.push_back(0x00);
  resp.push_back(0x00);
  // `NSCOUNT`=0, `ARCOUNT`=0.
  resp.push_back(0x00);
  resp.push_back(0x00);
  resp.push_back(0x00);
  resp.push_back(0x00);

  // Echo the question section.
  resp.insert(resp.end(), query + kDnsHeaderSize, query + question_end);

  return resp;
}

} // namespace Test
} // namespace Network
} // namespace Envoy
