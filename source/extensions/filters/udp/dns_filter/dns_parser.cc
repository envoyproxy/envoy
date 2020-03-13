#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include <iomanip>
#include <sstream>

#include "envoy/network/address.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

inline void DnsObject::dumpBuffer(const std::string& title, const Buffer::InstancePtr& buffer,
                                  const uint64_t offset) {
  const uint64_t data_length = buffer->length();
  unsigned char buf[1024] = {};
  unsigned char c;
  char* p = nullptr;

  p = reinterpret_cast<char*>(buf);

  for (uint64_t i = offset; i < data_length; i++) {
    if (i && ((i - offset) % 16 == 0)) {
      p += sprintf(p, "\n");
    }
    c = buffer->peekBEInt<unsigned char>(i);
    p += sprintf(p, "0x%02x ", c);
  }
  ENVOY_LOG_MISC(trace, "{}\n{}", title, buf);
}

inline void DnsObject::dumpFlags(const DnsMessageStruct& queryObj) {
  std::stringstream ss{};

  ss << "Query ID: 0x" << std::hex << queryObj.id << "\n";
  ss << "Flags: " << queryObj.f.val << std::dec << "\n";
  ss << "- Query/Response:       " << queryObj.f.flags.qr << "\n";
  ss << "- Opcode:               " << queryObj.f.flags.opcode << "\n";
  ss << "- Authoritative Answer: " << queryObj.f.flags.aa << "\n";
  ss << "- Truncated:            " << queryObj.f.flags.tc << "\n";
  ss << "- Recursion Desired:    " << queryObj.f.flags.rd << "\n";
  ss << "- Recursion Available:  " << queryObj.f.flags.ra << "\n";
  ss << "- Z bit:                " << queryObj.f.flags.z << "\n";
  ss << "- Authenticated Data    " << queryObj.f.flags.ad << "\n";
  ss << "- Checking Disabled     " << queryObj.f.flags.cd << "\n";
  ss << "- Return Code           " << queryObj.f.flags.rcode << "\n";
  ss << "Questions               " << queryObj.questions << "\n";
  ss << "Answers                 " << queryObj.answers << "\n";
  ss << "Authority RRs           " << queryObj.authority_rrs << "\n";
  ss << "Additional RRs          " << queryObj.additional_rrs << "\n";

  const std::string message = ss.str();

  ENVOY_LOG_MISC(trace, "{}", message);
}

inline void BaseDnsRecord::serializeName() {

  // This function iterates over a name e.g. "www.foo1.com"
  // once and produces a buffer with each name segment beginning
  // with its length. The buffer is terminated with a null byte

  size_t last = 0;
  size_t count = name_.find_first_of('.');

  auto iter = name_.begin();
  while (count != std::string::npos) {

    count -= last;
    buffer_.writeBEInt<uint8_t>(count);
    for (size_t i = 0; i < count; i++) {
      buffer_.writeByte(*iter);
      ++iter;
    }

    // periods are not serialized. Skip to the next character
    if (*iter == '.') {
      ++iter;
    }

    // Move our last marker to the next character
    // after where we stopped. Search for the next
    // name separator
    last += count;
    ++last;
    count = name_.find_first_of('.', last);
  }

  // Write the remaining segment prepended by its length
  count = name_.size() - last;
  buffer_.writeBEInt<uint8_t>(count);
  for (size_t i = 0; i < count; i++) {
    buffer_.writeByte(*iter);
    ++iter;
  }

  // Terminate the name record with a null byte
  buffer_.writeByte(0x00);
}

// TODO: How do we know that this worked?
Buffer::OwnedImpl& DnsQueryRecord::serialize() {
  // TODO: Reuse the existing serialized query.

  buffer_.drain(buffer_.length());

  serializeName();
  buffer_.writeBEInt<uint16_t>(type_);
  buffer_.writeBEInt<uint16_t>(class_);

  return buffer_;
}

Buffer::OwnedImpl& DnsAnswerRecord::serialize() {
  buffer_.drain(buffer_.length());

  serializeName();

  buffer_.writeBEInt<uint16_t>(type_);
  buffer_.writeBEInt<uint16_t>(class_);
  buffer_.writeBEInt<uint32_t>(ttl_);

  // Convert address and serialize
  auto address_ptr = Network::Utility::parseInternetAddress(address_, 0, false);
  auto ip_address = address_ptr->ip();

  if (ip_address->ipv6() != nullptr && data_length_ == 16) {
    buffer_.writeBEInt<uint16_t>(data_length_);
    buffer_.writeLEInt<uint32_t>(ip_address->ipv4()->address());
  } else if (ip_address->ipv4() != nullptr && data_length_ == 4) {
    buffer_.writeBEInt<uint16_t>(data_length_);
    buffer_.writeLEInt<uint32_t>(ip_address->ipv4()->address());
  }

  return buffer_;
}

bool DnsObject::parseDnsObject(const Buffer::InstancePtr& buffer) {

  auto available_bytes = buffer->length();
  ENVOY_LOG_MISC(debug, "In {} with {} bytes", __func__, available_bytes);

  dumpBuffer(__func__, buffer);

  memset(&incoming_, 0x00, sizeof(incoming_));
  queries_.clear();

  static const uint64_t field_size = sizeof(uint16_t);
  uint64_t offset = 0;
  uint16_t data;

  DnsQueryParseState state_{DnsQueryParseState::INIT};

  while (state_ != DnsQueryParseState::FINISH) {

    // Ensure that we have enough data remaining in the buffer
    // to parse the query
    if (available_bytes < field_size) {
      ENVOY_LOG_MISC(error,
                     "Exhausted available bytes in the buffer. Insufficient data to parse query");
      return false;
    }

    data = buffer->peekBEInt<uint16_t>(offset);
    offset += field_size;
    available_bytes -= field_size;

    if (offset > buffer->length()) {
      ENVOY_LOG_MISC(error, "Exhausted available bytes in the buffer. Unable to parse query");
      return false;
    }

    switch (state_) {
    case DnsQueryParseState::INIT:
      incoming_.id = data;
      state_ = DnsQueryParseState::FLAGS;
      break;

    case DnsQueryParseState::FLAGS:
      incoming_.f.val = data;
      state_ = DnsQueryParseState::QUESTIONS;
      break;

    case DnsQueryParseState::QUESTIONS:
      incoming_.questions = data;
      state_ = DnsQueryParseState::ANSWERS;
      break;

    case DnsQueryParseState::ANSWERS:
      incoming_.answers = data;
      state_ = DnsQueryParseState::AUTHORITY;
      break;

    case DnsQueryParseState::AUTHORITY:
      incoming_.authority_rrs = data;
      state_ = DnsQueryParseState::AUTHORITY2;
      break;

    case DnsQueryParseState::AUTHORITY2:
      incoming_.additional_rrs = data;
      state_ = DnsQueryParseState::FINISH;
      break;

    case DnsQueryParseState::FINISH:
      break;

    default:
      ENVOY_LOG_MISC(error, "Unknown DNS Query state: {}", as_integer(state_));
      return false;
    }
  }

  if (offset > buffer->length()) {
    ENVOY_LOG_MISC(error, "Data offset[{}] is larget than buffer size[{}]. Returning false", offset,
                   buffer->length());
    return false;
  }

  // DEBUG
  dumpFlags(incoming_);

  // Most time we will have only one query here.
  // TODO: Validate this and handle the case where more than one query is present
  for (uint16_t index = 0; index < incoming_.questions; index++) {
    auto rec = parseDnsQueryRecord(buffer, &offset);
    if (rec == nullptr) {
      ENVOY_LOG_MISC(error, "Couldn't parse query record from buffer");
      return false;
    }
    queries_.push_back(std::move(rec));
  }

  for (uint16_t index = 0; index < incoming_.answers; index++) {
    auto rec = parseDnsAnswerRecord(buffer, &offset);
    if (rec == nullptr) {
      ENVOY_LOG_MISC(error, "Couldn't parse answer record from buffer");
      return false;
    }
    answers_.push_back(std::move(rec));
  }

  return true;
}

const std::string DnsObject::parseDnsNameRecord(const Buffer::InstancePtr& buffer,
                                                uint64_t* available_bytes, uint64_t* name_offset) {
  std::stringstream name_ss{};
  unsigned char c;

  do {
    // Read the name segment length or flag;
    c = buffer->peekBEInt<unsigned char>(*name_offset);
    *name_offset += sizeof(unsigned char);
    *available_bytes -= sizeof(unsigned char);

    if (c == 0xc0) {
      // This is a compressed response. Get the offset in the query record
      // of the response where the domain name begins
      c = buffer->peekBEInt<unsigned char>(*name_offset);

      // We will restart the loop from this offset and read until we encounter
      // a null byte signifying the end of the name
      *name_offset = static_cast<uint64_t>(c);

      continue;

    } else if (c == 0x00) {
      // We've reached the end of the query.
      ENVOY_LOG_MISC(debug, "End of name: {}", c, *name_offset);
      break;
    }

    const uint64_t segment_length = static_cast<uint64_t>(c);

    // Verify that we have enough data to read the segment length
    if (*available_bytes < segment_length) {
      ENVOY_LOG_MISC(error,
                     "Insufficient data in buffer for name segment. "
                     "available bytes: {}  segment: {}",
                     *available_bytes, segment_length);
      return EMPTY_STRING;
    }

    // Add the name separator if we have already accumulated name data
    if (name_ss.tellp()) {
      name_ss << '.';
    }

    *available_bytes -= segment_length;

    // The value read is a name segment length
    for (uint64_t index = 0; index < segment_length; index++) {
      c = buffer->peekBEInt<unsigned char>(*name_offset);
      *name_offset += sizeof(unsigned char);
      name_ss << c;
    }

  } while (c != 0x00);

  const std::string name = name_ss.str();

  return name;
}

DnsAnswerRecordPtr DnsObject::parseDnsAnswerRecord(const Buffer::InstancePtr& buffer,
                                                   uint64_t* offset) {

  uint64_t data_offset = *offset;
  uint64_t available_bytes = buffer->length() - data_offset;

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &data_offset);
  if (record_name.empty()) {
    ENVOY_LOG_MISC(error, "Unable to parse name record from buffer");
    return nullptr;
  }

  if (available_bytes < (sizeof(uint32_t) + 3 * sizeof(uint16_t))) {
    ENVOY_LOG_MISC(error,
                   "Insufficient data in buffer to read answer record data."
                   "Available bytes: {}",
                   available_bytes);
    return nullptr;
  }

  //  Parse the type
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(record_type);
  available_bytes -= sizeof(record_type);

  //  Parse the class
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(record_class);
  available_bytes -= sizeof(record_class);

  //  Parse the TTL
  uint32_t ttl;
  ttl = buffer->peekBEInt<uint32_t>(data_offset);
  data_offset += sizeof(ttl);
  available_bytes -= sizeof(ttl);

  //  Parse the Data Length and Data record
  uint16_t data_length;
  data_length = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(data_length);
  available_bytes -= sizeof(data_length);

  if (available_bytes < data_length) {
    ENVOY_LOG_MISC(error,
                   "Answer record data length: {} is more than the available bytes {} in buffer",
                   data_length, available_bytes);
    return nullptr;
  }

  std::string address{};

  if (record_type == DnsRecordType::CNAME) {
    address = parseDnsNameRecord(buffer, &available_bytes, &data_offset);
  } else if (record_type == DnsRecordType::A) {
    sockaddr_in address4;
    address4.sin_addr.s_addr = buffer->peekLEInt<uint32_t>(data_offset);

    address = Network::Address::Ipv4Instance::sockaddrToString(address4);

  } else if (record_type == DnsRecordType::AAAA) {

    sockaddr_in6 address6;
    uint8_t* address6_bytes = reinterpret_cast<uint8_t*>(&address6.sin6_addr.s6_addr);
    for (size_t index = 0; index < sizeof(absl::uint128) / sizeof(uint8_t); index++) {
      *address6_bytes++ = buffer->peekLEInt<uint8_t>(data_offset++);
    }

    Network::Address::Ipv6Instance ip6(address6);
    address = ip6.ip()->addressAsString();
  }
  data_offset += data_length;

  ENVOY_LOG_MISC(debug, "Parsed address [{}] from record type [{}]", address, record_type);

  if (address.empty()) {
    ENVOY_LOG_MISC(error, "Could not parse address from record");
    return nullptr;
  }

  *offset = data_offset;

  auto rec = std::make_unique<DnsAnswerRecord>(record_name, record_type, record_class, ttl,
                                               data_length, address);

  return rec;
}

DnsQueryRecordPtr DnsObject::parseDnsQueryRecord(const Buffer::InstancePtr& buffer,
                                                 uint64_t* offset) {
  uint64_t name_offset = *offset;
  uint64_t available_bytes = buffer->length() - name_offset;

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &name_offset);
  if (record_name.empty()) {
    ENVOY_LOG_MISC(error, "Unable to parse name record from buffer");
    return nullptr;
  }

  if (available_bytes < 2 * sizeof(uint16_t)) {
    ENVOY_LOG_MISC(error,
                   "Insufficient data in buffer to read query record type and class. "
                   "Available bytes: {}",
                   available_bytes);
    return nullptr;
  }

  // Read the record type (A or AAAA)
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_type);

  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_class);

  auto rec = std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);

  // stop reading here since we aren't parsing additional records
  ENVOY_LOG_MISC(trace, "Extracted query record. Name: {} type: {} class: {}", rec->name_,
                 rec->type_, rec->class_);

  *offset = name_offset;

  return rec;
}

void DnsQueryParser::setDnsResponseFlags() {

  // Copy the transaction ID
  generated_.id = incoming_.id;

  // Signify that this is a response to a query
  generated_.f.flags.qr = 1;

  generated_.f.flags.opcode = incoming_.f.flags.opcode;

  generated_.f.flags.aa = 0;
  generated_.f.flags.tc = 0;

  // Copy Recursion flags
  generated_.f.flags.rd = incoming_.f.flags.rd;

  // TODO: This should be predicated on whether the user allows upstream lookups
  generated_.f.flags.ra = 0;

  // reserved flag is not set
  generated_.f.flags.z = 0;

  // Set the authenticated flags to zero
  generated_.f.flags.ad = 0;

  generated_.f.flags.cd = 0;

  generated_.answers = answers_.size();

  // If the ID is empty, the query was not parsed correctly or
  // it may be an invalid buffer
  if (incoming_.id == 0) {
    generated_.f.flags.rcode = as_integer(DnsResponseCode::FORMAT_ERROR);
  } else {
    generated_.f.flags.rcode =
        as_integer(answers_.empty() ? DnsResponseCode::NAME_ERROR : DnsResponseCode::NO_ERROR);
  }

  // Set the number of questions we are responding to
  generated_.questions = incoming_.questions;

  // We will not include any additional records
  generated_.authority_rrs = 0;
  generated_.additional_rrs = 0;

  // DEBUG
  dumpFlags(generated_);
}

// This is the sole function that aggregates all the data and builds
// the buffer sent back to the client
bool DnsQueryParser::buildResponseBuffer(Buffer::OwnedImpl& buffer_,
                                         DnsAnswerRecordPtr& answer_record) {

  // Build the response and send it on the connection
  ENVOY_LOG(debug, "In {} with address [{}]", __func__,
            answer_record != nullptr ? answer_record->address_ : "Nothing");

  answers_.clear();
  if (answer_record != nullptr) {
    answers_.push_back(std::move(answer_record));
  }

  // Clear any left over cruft
  buffer_.drain(buffer_.length());

  setDnsResponseFlags();

  buffer_.writeBEInt<uint16_t>(generated_.id);
  buffer_.writeBEInt<uint16_t>(generated_.f.val);
  buffer_.writeBEInt<uint16_t>(generated_.questions);
  buffer_.writeBEInt<uint16_t>(generated_.answers);
  buffer_.writeBEInt<uint16_t>(generated_.authority_rrs);
  buffer_.writeBEInt<uint16_t>(generated_.additional_rrs);

  // Copy the query that we are answering into to the response

  // TODO: Find a way to copy this from the original buffer so that we aren't
  // re-serializing the data. We do this for a couple reasons. Pointer ownership
  // gets a bit hairy when trying to store an offset and a pointer to the original
  // buffer. Secondly, if there are additional records in the original query
  // we aren't parsing those, so we don't know the complete length of the query.
  if (!queries_.empty()) {
    buffer_.add(queries_.front()->serialize());
  }

  // serialize the answer records and add to the buffer here
  for (const auto& answer_rec : answers_) {
    buffer_.add(answer_rec->serialize());
  }

  return true;
}

bool DnsResponseParser::parseResponseData(const Buffer::InstancePtr& buffer) {
  const uint64_t data_length = buffer->length();
  ENVOY_LOG(debug, "In {} with {} bytes", __func__, data_length);

  dumpBuffer("Response", buffer);

  // Successfully able to parse the response
  return parseDnsObject(buffer);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
