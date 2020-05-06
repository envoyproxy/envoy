#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include <iomanip>
#include <sstream>

#include "envoy/network/address.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

// TODO(abaptiste): add fuzzing tests for DNS message parsing
#include "ares.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

void BaseDnsRecord::serializeName(Buffer::OwnedImpl& output) {
  // Iterate over a name e.g. "www.domain.com" once and produce a buffer containing each name
  // segment prefixed by its length
  static constexpr char SEPARATOR('.');

  size_t last = 0;
  size_t count = name_.find_first_of(SEPARATOR);
  auto iter = name_.begin();

  while (count != std::string::npos) {
    count -= last;
    output.writeBEInt<uint8_t>(count);
    for (size_t i = 0; i < count; i++) {
      output.writeByte(*iter);
      ++iter;
    }

    // periods are not serialized. Skip to the next character
    if (*iter == SEPARATOR) {
      ++iter;
    }

    // Move our last marker to the first position after where we stopped. Search for the next name
    // separator
    last += count;
    ++last;
    count = name_.find_first_of(SEPARATOR, last);
  }

  // Write the remaining segment prepended by its length
  count = name_.size() - last;
  output.writeBEInt<uint8_t>(count);
  for (size_t i = 0; i < count; i++) {
    output.writeByte(*iter);
    ++iter;
  }

  // Terminate the name record with a null byte
  output.writeByte(0x00);
}

// Serialize a DNS Query Record
void DnsQueryRecord::serialize(Buffer::OwnedImpl& output) {
  serializeName(output);
  output.writeBEInt<uint16_t>(type_);
  output.writeBEInt<uint16_t>(class_);
}

DnsQueryContextPtr DnsMessageParser::createQueryContext(Network::UdpRecvData& client_request) {
  DnsQueryContextPtr query_context = std::make_unique<DnsQueryContext>(
      client_request.addresses_.local_, client_request.addresses_.peer_);

  query_context->parse_status_ = parseDnsObject(query_context, client_request.buffer_);
  if (!query_context->parse_status_) {
    ENVOY_LOG(debug, "Unable to parse query buffer from '{}' into a DNS object.",
              client_request.addresses_.peer_->ip()->addressAsString());
  }

  return query_context;
}

bool DnsMessageParser::parseDnsObject(DnsQueryContextPtr& context,
                                      const Buffer::InstancePtr& buffer) {
  static constexpr uint64_t field_size = sizeof(uint16_t);
  size_t available_bytes = buffer->length();
  uint64_t offset = 0;
  uint16_t data;
  DnsQueryParseState state{DnsQueryParseState::Init};

  header_ = {};
  while (state != DnsQueryParseState::Finish) {
    // Ensure that we have enough data remaining in the buffer to parse the query
    if (available_bytes < field_size) {
      ENVOY_LOG(debug,
                "Exhausted available bytes in the buffer. Insufficient data to parse query field.");
      return false;
    }

    // Each aggregate DNS header field is 2 bytes wide.
    data = buffer->peekBEInt<uint16_t>(offset);
    offset += field_size;
    available_bytes -= field_size;

    if (offset > buffer->length()) {
      ENVOY_LOG(debug, "Buffer read offset [{}] is beyond buffer length [{}].", offset,
                buffer->length());
      return false;
    }

    switch (state) {
    case DnsQueryParseState::Init:
      header_.id = data;
      state = DnsQueryParseState::Flags;
      break;

    case DnsQueryParseState::Flags:
      ::memcpy(static_cast<void*>(&header_.flags), &data, sizeof(uint16_t));
      state = DnsQueryParseState::Questions;
      break;

    case DnsQueryParseState::Questions:
      header_.questions = data;
      state = DnsQueryParseState::Answers;
      break;

    case DnsQueryParseState::Answers:
      header_.answers = data;
      state = DnsQueryParseState::Authority;
      break;

    case DnsQueryParseState::Authority:
      header_.authority_rrs = data;
      state = DnsQueryParseState::Authority2;
      break;

    case DnsQueryParseState::Authority2:
      header_.additional_rrs = data;
      state = DnsQueryParseState::Finish;
      break;

    case DnsQueryParseState::Finish:
      break;
    }
  }

  // TODO(abaptiste):  Verify that queries do not contain answer records
  // Verify that we still have available data in the buffer to read answer and query records
  if (offset > buffer->length()) {
    ENVOY_LOG(debug, "Buffer read offset[{}] is larget than buffer length [{}].", offset,
              buffer->length());
    return false;
  }

  context->id_ = static_cast<uint16_t>(header_.id);

  // Almost always, we will have only one query here. Per the RFC, QDCOUNT is usually 1
  context->queries_.reserve(header_.questions);
  for (auto index = 0; index < header_.questions; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] questions", index, header_.questions);
    auto rec = parseDnsQueryRecord(buffer, &offset);
    if (rec == nullptr) {
      ENVOY_LOG(debug, "Couldn't parse query record from buffer");
      return false;
    }
    context->queries_.push_back(std::move(rec));
  }

  return true;
}

const std::string DnsMessageParser::parseDnsNameRecord(const Buffer::InstancePtr& buffer,
                                                       uint64_t* available_bytes,
                                                       uint64_t* name_offset) {
  void* buf = buffer->linearize(static_cast<uint32_t>(buffer->length()));
  const unsigned char* linearized_data = static_cast<const unsigned char*>(buf);
  const unsigned char* record = linearized_data + *name_offset;
  long encoded_len;
  char* output;

  int result = ares_expand_name(record, linearized_data, buffer->length(), &output, &encoded_len);
  if (result != ARES_SUCCESS) {
    return EMPTY_STRING;
  }

  std::string name(output);
  ares_free_string(output);
  *name_offset += encoded_len;
  *available_bytes -= encoded_len;

  return name;
}

DnsQueryRecordPtr DnsMessageParser::parseDnsQueryRecord(const Buffer::InstancePtr& buffer,
                                                        uint64_t* offset) {
  uint64_t name_offset = *offset;
  uint64_t available_bytes = buffer->length() - name_offset;

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &name_offset);
  if (record_name.empty()) {
    ENVOY_LOG(debug, "Unable to parse name record from buffer");
    return nullptr;
  }

  if (available_bytes < 2 * sizeof(uint16_t)) {
    ENVOY_LOG(debug, "Insufficient data in buffer to read query record type and class. ");
    return nullptr;
  }

  // Read the record type (A or AAAA)
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_type);

  // Read the record class. This value is almost always 1 for internet address records
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_class);

  auto rec = std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);

  // stop reading he buffer here since we aren't parsing additional records
  ENVOY_LOG(trace, "Extracted query record. Name: {} type: {} class: {}", rec->name_, rec->type_,
            rec->class_);

  *offset = name_offset;

  return rec;
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
