#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

namespace {
template <typename Enumeration>
auto as_integer(Enumeration const value) -> typename std::underlying_type<Enumeration>::type {
  return static_cast<typename std::underlying_type<Enumeration>::type>(value);
}
} // namespace

// The flags have been verified with dig and this
// structure should not be modified. The flag order
// does not match the RFC, but takes byte ordering
// into account so that serialization/deserialization
// requires no and-ing or shifting.
PACKED_STRUCT(struct dns_query_flags_s {
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

using dns_query_flags_t = struct dns_query_flags_s;

PACKED_STRUCT(struct dns_query_s {
  uint16_t id;
  union {
    uint16_t val;
    dns_query_flags_t flags;
  } f;
  uint16_t questions;
  uint16_t answers;
  uint16_t authority_rrs;
  uint16_t additional_rrs;
});

using DnsMessageStruct = struct dns_query_s;

enum DnsRecordClass { IN = 1 };
enum DnsRecordType { A = 1, CNAME = 5, AAAA = 28 };
enum class DnsResponseCode { NO_ERROR, FORMAT_ERROR, SERVER_FAILURE, NAME_ERROR, NOT_IMPLEMENTED };

// BaseDnsRecord class containing the domain name operated on, its class, and address type
// Since this is IP based the class is almost always 1 (INET), the type varies between
// A and AAAA queries
class BaseDnsRecord {
public:
  BaseDnsRecord(const std::string& rec_name, const uint16_t rec_type, const uint16_t rec_class)
      : name_(rec_name), type_(rec_type), class_(rec_class) {}

  virtual ~BaseDnsRecord() {}

  const std::string name_;
  const uint16_t type_;
  const uint16_t class_;

  void serializeName();

protected:
  virtual Buffer::OwnedImpl& serialize() PURE;
  Buffer::OwnedImpl buffer_;
};

class DnsQueryRecord : public BaseDnsRecord {

public:
  DnsQueryRecord(const std::string& rec_name, const uint16_t rec_type, const uint16_t rec_class)
      : BaseDnsRecord(rec_name, rec_type, rec_class) {}

  virtual ~DnsQueryRecord() {}
  virtual Buffer::OwnedImpl& serialize();
};

using DnsQueryRecordPtr = std::unique_ptr<DnsQueryRecord>;
using DnsQueryList = std::list<DnsQueryRecordPtr>;

class DnsAnswerRecord : public BaseDnsRecord {
public:
  DnsAnswerRecord(const std::string& query_name, const uint16_t rec_type, const uint16_t rec_class,
                  const uint32_t ttl, const uint16_t data_length, const std::string& address)
      : BaseDnsRecord(query_name, rec_type, rec_class), ttl_(ttl), data_length_(data_length),
        address_(address) {}

  virtual ~DnsAnswerRecord() {}
  virtual Buffer::OwnedImpl& serialize();

  const uint32_t ttl_;
  const uint16_t data_length_;
  const std::string address_;
};

using DnsAnswerRecordPtr = std::unique_ptr<DnsAnswerRecord>;
using DnsAnswerList = std::list<DnsAnswerRecordPtr>;

enum class DnsQueryParseState {
  INIT = 0,
  TRANSACTION_ID, // 2 bytes
  FLAGS,          // 2 bytes
  QUESTIONS,      // 2 bytes
  ANSWERS,        // 2 bytes
  AUTHORITY,      // 2 bytes
  AUTHORITY2,     // 2 bytes
  FINISH
};

class DnsObject {

public:
  DnsObject() : queries_(), answers_() {}
  virtual ~DnsObject(){};

  bool parseDnsObject(const Buffer::InstancePtr& buffer);
  DnsQueryRecordPtr parseDnsQueryRecord(const Buffer::InstancePtr& buffer, uint64_t* offset);
  DnsAnswerRecordPtr parseDnsAnswerRecord(const Buffer::InstancePtr& buffer, uint64_t* offset);
  const DnsQueryList& getQueries() { return queries_; }
  const DnsAnswerList& getAnswers() { return answers_; }

  uint16_t getQueryResponseCode() { return static_cast<uint16_t>(incoming_.f.flags.rcode); }
  uint16_t getAnswerResponseCode() { return static_cast<uint16_t>(generated_.f.flags.rcode); }

  void dumpBuffer(const std::string& title, const Buffer::InstancePtr& buffer,
                  const uint64_t offset = 0);
  void dumpFlags(const DnsMessageStruct& queryObj);

  DnsMessageStruct incoming_;
  DnsMessageStruct generated_;

  DnsQueryList queries_;
  DnsAnswerList answers_;

private:
  const std::string parseDnsNameRecord(const Buffer::InstancePtr& buffer, uint64_t* available_bytes,
                                       uint64_t* name_offset);
};

class DnsQueryParser : public DnsObject, Logger::Loggable<Logger::Id::filter> {
public:
  virtual bool buildResponseBuffer(Buffer::OwnedImpl& buffer, DnsAnswerRecordPtr& answer_rec);

private:
  void setDnsResponseFlags();
};

using DnsQueryParserPtr = std::unique_ptr<DnsQueryParser>;

class DnsResponseParser : public DnsObject, Logger::Loggable<Logger::Id::filter> {

public:
  virtual bool parseResponseData(const Buffer::InstancePtr& buffer);
};

using DnsResponseParserPtr = std::unique_ptr<DnsResponseParser>;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
