#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

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

enum DnsRecordClass { IN = 1 };
enum DnsRecordType { A = 1, AAAA = 28 };
enum DnsResponseCode { NoError, FormatError, ServerFailure, NameError, NotImplemented };

/**
 * BaseDnsRecord contains the fields and functions common to both query and answer records.
 */
class BaseDnsRecord {
public:
  BaseDnsRecord(const uint16_t id, const std::string& rec_name, const uint16_t rec_type,
                const uint16_t rec_class)
      : id_(id), name_(rec_name), type_(rec_type), class_(rec_class){};

  virtual ~BaseDnsRecord() = default;
  void serializeName();
  virtual void serialize(Buffer::OwnedImpl& output) PURE;

  const uint16_t id_;
  const std::string name_;
  const uint16_t type_;
  const uint16_t class_;

protected:
  Buffer::OwnedImpl buffer_;
};

/**
 * DnsQueryRecord represents a query record parsed from a DNS request from a client. Each record
 * contains the ID, domain requested and the flags dictating the type of record that is sought.
 */
class DnsQueryRecord : public BaseDnsRecord {

public:
  DnsQueryRecord(const uint16_t id, const std::string& rec_name, const uint16_t rec_type,
                 const uint16_t rec_class)
      : BaseDnsRecord(id, rec_name, rec_type, rec_class) {}

  ~DnsQueryRecord() override = default;
  void serialize(Buffer::OwnedImpl& output) override;
};

using DnsQueryRecordPtr = std::shared_ptr<DnsQueryRecord>;
using DnsQueryMap = absl::flat_hash_map<uint16_t, std::list<DnsQueryRecordPtr>>;

using AddressConstPtrVec = std::vector<Network::Address::InstanceConstSharedPtr>;
using AnswerCallback = std::function<void(DnsQueryRecordPtr& query, AddressConstPtrVec& ipaddr)>;

/**
 * DnsAnswerRecord represents a single answer record for a name that is to be serialized and sent to
 * a client. This class differs from the BaseDnsRecord and DnsQueryRecord because it contains
 * additional fields for the TTL and address.
 */
class DnsAnswerRecord : public BaseDnsRecord {
public:
  DnsAnswerRecord(const uint16_t id, const std::string& query_name, const uint16_t rec_type,
                  const uint16_t rec_class, const uint32_t ttl,
                  Network::Address::InstanceConstSharedPtr ipaddr)
      : BaseDnsRecord(id, query_name, rec_type, rec_class), ttl_(ttl), ip_addr_(ipaddr) {}

  ~DnsAnswerRecord() override = default;
  void serialize(Buffer::OwnedImpl& output) override { UNREFERENCED_PARAMETER(output); }

  const uint32_t ttl_;
  Network::Address::InstanceConstSharedPtr ip_addr_;
};

using DnsAnswerRecordPtr = std::unique_ptr<DnsAnswerRecord>;
using DnsAnswerMap = absl::flat_hash_map<std::string, std::list<DnsAnswerRecordPtr>>;

enum class DnsQueryParseState {
  Init = 0,
  Flags,      // 2 bytes
  Questions,  // 2 bytes
  Answers,    // 2 bytes
  Authority,  // 2 bytes
  Authority2, // 2 bytes
  Finish
};

/**
 * This class orchestrates parsing a DNS query and building the response to be sent to a client.
 */
class DnsMessageParser : public Logger::Loggable<Logger::Id::filter> {

public:
  DnsMessageParser() = default;
  ~DnsMessageParser() = default;

  /**
   * @param buffer a reference to the incoming request object received by the listener
   * @return bool true if all DNS records and flags were successfully parsed from the buffer
   */
  bool parseDnsObject(const Buffer::InstancePtr& buffer);

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
   * @return a reference to a list of queries parsed from a client request
   */
  const DnsQueryMap& getActiveQueryRecords() { return queries_; }

  /**
   * @return the current query transaction ID being handled
   */
  uint16_t getCurrentQueryId() const { return active_transactions_.front(); }

  /**
   * @return uint16_t the response code flag value from a parsed dns object
   */
  uint16_t getQueryResponseCode() { return static_cast<uint16_t>(incoming_.flags.rcode); }

  /**
   * Reset the internal state of the class.
   */
  void reset() {
    active_transactions_.clear();
    queries_.clear();
  }

private:
  const std::string parseDnsNameRecord(const Buffer::InstancePtr& buffer, uint64_t* available_bytes,
                                       uint64_t* name_offset);

  /**
   * @brief updates a map associating a query id with a list of DnsQueryRecord pointers
   *
   * @param rec the answer record that is to be added to the answer list
   */
  void storeQueryRecord(DnsQueryRecordPtr rec);

  struct DnsHeader incoming_;
  std::deque<uint16_t> active_transactions_;

  DnsQueryMap queries_;
};

using DnsMessageParserPtr = std::unique_ptr<DnsMessageParser>;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
