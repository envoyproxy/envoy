#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/network/address.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

constexpr uint16_t DNS_RECORD_CLASS_IN = 1;
constexpr uint16_t DNS_RECORD_TYPE_A = 1;
constexpr uint16_t DNS_RECORD_TYPE_AAAA = 28;

constexpr uint16_t DNS_RESPONSE_CODE_NO_ERROR = 0;
constexpr uint16_t DNS_RESPONSE_CODE_FORMAT_ERROR = 1;
constexpr uint16_t DNS_RESPONSE_CODE_SERVER_FAILURE = 2;
constexpr uint16_t DNS_RESPONSE_CODE_NAME_ERROR = 3;
constexpr uint16_t DNS_RESPONSE_CODE_NOT_IMPLEMENTED = 4;

/**
 * BaseDnsRecord contains the fields and functions common to both query and answer records.
 */
class BaseDnsRecord {
public:
  BaseDnsRecord(const std::string& rec_name, const uint16_t rec_type, const uint16_t rec_class)
      : name_(rec_name), type_(rec_type), class_(rec_class){};

  virtual ~BaseDnsRecord() = default;
  void serializeName(Buffer::OwnedImpl& output);
  virtual void serialize(Buffer::OwnedImpl& output) PURE;

  const std::string name_;
  const uint16_t type_;
  const uint16_t class_;
};

/**
 * DnsQueryRecord represents a query record parsed from a DNS request from a client. Each record
 * contains the ID, domain requested and the flags dictating the type of record that is sought.
 */
class DnsQueryRecord : public BaseDnsRecord {
public:
  DnsQueryRecord(const std::string& rec_name, const uint16_t rec_type, const uint16_t rec_class)
      : BaseDnsRecord(rec_name, rec_type, rec_class) {}
  void serialize(Buffer::OwnedImpl& output) override;
};

using DnsQueryRecordPtr = std::unique_ptr<DnsQueryRecord>;
using DnsQueryPtrVec = std::vector<DnsQueryRecordPtr>;

using AddressConstPtrVec = std::vector<Network::Address::InstanceConstSharedPtr>;
using AnswerCallback = std::function<void(DnsQueryRecordPtr& query, AddressConstPtrVec& ipaddr)>;

/**
 * DnsAnswerRecord represents a single answer record for a name that is to be serialized and sent to
 * a client. This class differs from the BaseDnsRecord and DnsQueryRecord because it contains
 * additional fields for the TTL and address.
 */
class DnsAnswerRecord : public BaseDnsRecord {
public:
  DnsAnswerRecord(const std::string& query_name, const uint16_t rec_type, const uint16_t rec_class,
                  const uint32_t ttl, Network::Address::InstanceConstSharedPtr ipaddr)
      : BaseDnsRecord(query_name, rec_type, rec_class), ttl_(ttl), ip_addr_(ipaddr) {}
  void serialize(Buffer::OwnedImpl& output) override { UNREFERENCED_PARAMETER(output); }

  const uint32_t ttl_;
  const Network::Address::InstanceConstSharedPtr ip_addr_;
};

using DnsAnswerRecordPtr = std::unique_ptr<DnsAnswerRecord>;
using DnsAnswerMap = std::unordered_multimap<std::string, DnsAnswerRecordPtr>;

/**
 * DnsQueryContext contains all the data associated with a query. The filter uses this object to
 * generate a response and determine where it should be transmitted.
 */
class DnsQueryContext {
public:
  DnsQueryContext(Network::Address::InstanceConstSharedPtr local,
                  Network::Address::InstanceConstSharedPtr peer)
      : local_(std::move(local)), peer_(std::move(peer)), parse_status_(false), id_() {}

  const Network::Address::InstanceConstSharedPtr local_;
  const Network::Address::InstanceConstSharedPtr peer_;
  bool parse_status_;
  uint16_t id_;
  DnsQueryPtrVec queries_;
  DnsAnswerMap answers_;
};

using DnsQueryContextPtr = std::unique_ptr<DnsQueryContext>;

/**
 * This class orchestrates parsing a DNS query and building the response to be sent to a client.
 */
class DnsMessageParser : public Logger::Loggable<Logger::Id::filter> {
public:
  enum class DnsQueryParseState {
    Init = 0,
    Flags,      // 2 bytes
    Questions,  // 2 bytes
    Answers,    // 2 bytes
    Authority,  // 2 bytes
    Authority2, // 2 bytes
    Finish
  };

  // These flags have been verified with dig. The flag order does not match the RFC, but takes byte
  // ordering into account so that serialization does not need bitwise operations
  PACKED_STRUCT(struct DnsHeaderFlags {
    unsigned rcode : 4;  // return code
    unsigned cd : 1;     // checking disabled
    unsigned ad : 1;     // authenticated data
    unsigned z : 1;      // z - bit (must be zero in queries per RFC1035)
    unsigned ra : 1;     // recursion available
    unsigned rd : 1;     // recursion desired
    unsigned tc : 1;     // truncated response
    unsigned aa : 1;     // authoritative answer
    unsigned opcode : 4; // operation code
    unsigned qr : 1;     // query or response
  });

  /**
   * Structure representing the DNS header as it appears in a packet
   * See https://www.ietf.org/rfc/rfc1035.txt for more details
   */
  PACKED_STRUCT(struct DnsHeader {
    uint16_t id;
    struct DnsHeaderFlags flags;
    uint16_t questions;
    uint16_t answers;
    uint16_t authority_rrs;
    uint16_t additional_rrs;
  });

  /**
   * @brief parse a single query record from a client request
   *
   * @param buffer a reference to the incoming request object received by the listener
   * @param offset the buffer offset at which parsing is to begin. This parameter is updated when
   * one record is parsed from the buffer and returned to the caller.
   * @return DnsQueryRecordPtr a pointer to a DnsQueryRecord object containing all query data parsed
   * from the buffer
   */
  DnsQueryRecordPtr parseDnsQueryRecord(const Buffer::InstancePtr& buffer, uint64_t* offset);

  /**
   * @return uint16_t the response code flag value from a parsed dns object
   */
  uint16_t getQueryResponseCode() { return static_cast<uint16_t>(header_.flags.rcode); }

  /**
   * @brief Create a context object for handling a DNS Query
   *
   * @param client_request the context containing the client addressing and the buffer with the DNS
   * query contents
   */
  DnsQueryContextPtr createQueryContext(Network::UdpRecvData& client_request);

private:
  /**
   * @param buffer a reference to the incoming request object received by the listener
   * @return bool true if all DNS records and flags were successfully parsed from the buffer
   */
  bool parseDnsObject(DnsQueryContextPtr& context, const Buffer::InstancePtr& buffer);

  const std::string parseDnsNameRecord(const Buffer::InstancePtr& buffer, uint64_t* available_bytes,
                                       uint64_t* name_offset);

  DnsHeader header_;
};

using DnsMessageParserPtr = std::unique_ptr<DnsMessageParser>;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
