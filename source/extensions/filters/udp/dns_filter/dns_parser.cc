#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include "envoy/network/address.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "ares.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

bool BaseDnsRecord::serializeName(Buffer::OwnedImpl& output) {
  // Iterate over a name e.g. "www.domain.com" once and produce a buffer containing each name
  // segment prefixed by its length
  static constexpr char SEPARATOR = '.';
  static constexpr size_t MAX_LABEL_LENGTH = 63;
  static constexpr size_t MAX_NAME_LENGTH = 255;

  // Names are restricted to 255 bytes per RFC
  if (name_.size() > MAX_NAME_LENGTH) {
    return false;
  }

  size_t last = 0;
  size_t count = name_.find_first_of(SEPARATOR);
  auto iter = name_.begin();

  while (count != std::string::npos) {
    if ((count - last) > MAX_LABEL_LENGTH) {
      return false;
    }

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
  return true;
}

// Serialize a DNS Query Record
bool DnsQueryRecord::serialize(Buffer::OwnedImpl& output) {
  if (serializeName(output)) {
    output.writeBEInt<uint16_t>(type_);
    output.writeBEInt<uint16_t>(class_);
    return true;
  }
  return false;
}

// Serialize a DNS Answer Record
bool DnsAnswerRecord::serialize(Buffer::OwnedImpl& output) {
  if (serializeName(output)) {
    output.writeBEInt<uint16_t>(type_);
    output.writeBEInt<uint16_t>(class_);
    output.writeBEInt<uint32_t>(static_cast<uint32_t>(ttl_.count()));

    ASSERT(ip_addr_ != nullptr);
    const auto ip_address = ip_addr_->ip();

    ASSERT(ip_address != nullptr);
    if (ip_address->ipv6() != nullptr) {
      // Store the 128bit address with 2 64 bit writes
      const absl::uint128 addr6 = ip_address->ipv6()->address();
      output.writeBEInt<uint16_t>(sizeof(addr6));
      output.writeLEInt<uint64_t>(absl::Uint128Low64(addr6));
      output.writeLEInt<uint64_t>(absl::Uint128High64(addr6));
    } else if (ip_address->ipv4() != nullptr) {
      output.writeBEInt<uint16_t>(4);
      output.writeLEInt<uint32_t>(ip_address->ipv4()->address());
    }
    return true;
  }
  return false;
}

DnsQueryContextPtr DnsMessageParser::createQueryContext(Network::UdpRecvData& client_request,
                                                        DnsParserCounters& counters) {
  DnsQueryContextPtr query_context = std::make_unique<DnsQueryContext>(
      client_request.addresses_.local_, client_request.addresses_.peer_, counters, retry_count_);

  query_context->parse_status_ = parseDnsObject(query_context, client_request.buffer_);
  if (!query_context->parse_status_) {
    query_context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    ENVOY_LOG(debug, "Unable to parse query buffer from '{}' into a DNS object",
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
      context->counters_.underflow_counter.inc();
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

  if (!header_.flags.qr && header_.answers) {
    ENVOY_LOG(debug, "Answer records present in query");
    return false;
  }

  if (header_.questions > 1) {
    ENVOY_LOG(debug, "Multiple [{}] questions in DNS query", header_.questions);
    return false;
  }

  // Verify that we still have available data in the buffer to read answer and query records
  if (offset > buffer->length()) {
    ENVOY_LOG(debug, "Buffer read offset[{}] is larget than buffer length [{}].", offset,
              buffer->length());
    return false;
  }

  context->id_ = static_cast<uint16_t>(header_.id);
  if (context->id_ == 0) {
    ENVOY_LOG(debug, "No ID in DNS query");
    return false;
  }

  if (header_.questions == 0) {
    ENVOY_LOG(debug, "No questions in DNS request");
    return false;
  }

  // Almost always, we will have only one query here. Per the RFC, QDCOUNT is usually 1
  context->queries_.reserve(header_.questions);
  for (auto index = 0; index < header_.questions; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] questions", index, header_.questions);
    auto rec = parseDnsQueryRecord(buffer, &offset);
    if (rec == nullptr) {
      context->counters_.query_parsing_failure.inc();
      ENVOY_LOG(debug, "Couldn't parse query record from buffer");
      return false;
    }
    context->queries_.push_back(std::move(rec));
  }

  // Parse all answer records and store them. This is exercised primarily in tests to
  // verify the responses returned from the filter.
  for (auto index = 0; index < header_.answers; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] answers", index, header_.answers);
    auto rec = parseDnsAnswerRecord(buffer, &offset);
    if (rec == nullptr) {
      ENVOY_LOG(debug, "Couldn't parse answer record from buffer");
      return false;
    }
    const std::string name = rec->name_;
    context->answers_.emplace(name, std::move(rec));
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

DnsAnswerRecordPtr DnsMessageParser::parseDnsAnswerRecord(const Buffer::InstancePtr& buffer,
                                                          uint64_t* offset) {
  uint64_t data_offset = *offset;

  if (data_offset > buffer->length()) {
    ENVOY_LOG(debug, "Invalid offset for parsing answer record");
    return nullptr;
  }

  uint64_t available_bytes = buffer->length() - data_offset;

  if (available_bytes == 0) {
    ENVOY_LOG(debug, "No data left in buffer for reading answer record");
    return nullptr;
  }

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &data_offset);
  if (record_name.empty()) {
    ENVOY_LOG(debug, "Unable to parse name record from buffer");
    return nullptr;
  }

  if (available_bytes < (sizeof(uint32_t) + 3 * sizeof(uint16_t))) {
    ENVOY_LOG(debug,
              "Insufficient data in buffer to read answer record data."
              "Available bytes: {}",
              available_bytes);
    return nullptr;
  }

  // Parse the record type
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  // We support only A and AAAA record types
  if (record_type != DNS_RECORD_TYPE_A && record_type != DNS_RECORD_TYPE_AAAA) {
    ENVOY_LOG(debug, "Unsupported record type [{}] found in answer", record_type);
    return nullptr;
  }

  // Parse the record class
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  // We support only IN record classes
  if (record_class != DNS_RECORD_CLASS_IN) {
    ENVOY_LOG(debug, "Unsupported record class [{}] found in answer", record_class);
    return nullptr;
  }

  // Read the record's TTL
  uint32_t ttl;
  ttl = buffer->peekBEInt<uint32_t>(data_offset);
  data_offset += sizeof(uint32_t);
  available_bytes -= sizeof(uint32_t);

  // Parse the Data Length and address data record
  uint16_t data_length;
  data_length = buffer->peekBEInt<uint16_t>(data_offset);
  data_offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  if (data_length == 0) {
    ENVOY_LOG(debug, "Read zero for data length when reading address from answer record");
    return nullptr;
  }

  // Build an address pointer from the string data.
  // We don't support anything other than A or AAAA records. If we add support for other record
  // types, we must account for them here
  Network::Address::InstanceConstSharedPtr ip_addr = nullptr;

  switch (record_type) {
  case DNS_RECORD_TYPE_A:
    if (available_bytes >= sizeof(uint32_t)) {
      sockaddr_in sa4;
      sa4.sin_addr.s_addr = buffer->peekLEInt<uint32_t>(data_offset);
      ip_addr = std::make_shared<Network::Address::Ipv4Instance>(&sa4);
      data_offset += data_length;
    }
    break;
  case DNS_RECORD_TYPE_AAAA:
    if (available_bytes >= sizeof(absl::uint128)) {
      sockaddr_in6 sa6;
      uint8_t* address6_bytes = reinterpret_cast<uint8_t*>(&sa6.sin6_addr.s6_addr);
      static constexpr size_t count = sizeof(absl::uint128) / sizeof(uint8_t);
      for (size_t index = 0; index < count; index++) {
        *address6_bytes++ = buffer->peekLEInt<uint8_t>(data_offset++);
      }
      ip_addr = std::make_shared<Network::Address::Ipv6Instance>(sa6, true);
    }
    break;
  default:
    ENVOY_LOG(debug, "Unsupported record type [{}] found in answer", record_type);
    break;
  }

  if (ip_addr == nullptr) {
    ENVOY_LOG(debug, "Unable to parse IP address from data in answer record");
    return nullptr;
  }

  ENVOY_LOG(trace, "Parsed address [{}] from record type [{}]: offset {}",
            ip_addr->ip()->addressAsString(), record_type, data_offset);

  *offset = data_offset;

  return std::make_unique<DnsAnswerRecord>(record_name, record_type, record_class,
                                           std::chrono::seconds(ttl), std::move(ip_addr));
}

DnsQueryRecordPtr DnsMessageParser::parseDnsQueryRecord(const Buffer::InstancePtr& buffer,
                                                        uint64_t* offset) {
  uint64_t name_offset = *offset;
  uint64_t available_bytes = buffer->length() - name_offset;

  if (available_bytes == 0) {
    ENVOY_LOG(debug, "No available data in buffer to parse a query record");
    return nullptr;
  }

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &name_offset);
  if (record_name.empty()) {
    ENVOY_LOG(debug, "Unable to parse name record from buffer [length {}]", buffer->length());
    return nullptr;
  }

  if (available_bytes < 2 * sizeof(uint16_t)) {
    ENVOY_LOG(debug,
              "Insufficient data in buffer to read query record type and class. "
              "Available bytes: {}",
              available_bytes);
    return nullptr;
  }

  // Read the record type (A or AAAA)
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_type);

  // Read the record class. This value is always 1 for internet address records
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_class);

  if (record_class != DNS_RECORD_CLASS_IN) {
    ENVOY_LOG(debug, "Unsupported record class '{}' in address record", record_class);
    return nullptr;
  }

  auto rec = std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);
  rec->query_time_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      query_latency_histogram_, timesource_);

  // stop reading the buffer here since we aren't parsing additional records
  ENVOY_LOG(trace, "Extracted query record. Name: {} type: {} class: {}", record_name, record_type,
            record_class);

  *offset = name_offset;
  return std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);
}

void DnsMessageParser::setDnsResponseFlags(DnsQueryContextPtr& query_context,
                                           const uint16_t questions, const uint16_t answers) {
  // Copy the transaction ID
  response_header_.id = header_.id;

  // Signify that this is a response to a query
  response_header_.flags.qr = 1;

  response_header_.flags.opcode = header_.flags.opcode;
  response_header_.flags.aa = 0;
  response_header_.flags.tc = 0;

  // Copy Recursion flags
  response_header_.flags.rd = header_.flags.rd;

  // Set the recursion flag based on whether Envoy is configured to forward queries
  response_header_.flags.ra = recursion_available_;

  // reserved flag is not set
  response_header_.flags.z = 0;

  // Set the authenticated flags to zero
  response_header_.flags.ad = 0;

  response_header_.flags.cd = 0;
  response_header_.answers = answers;
  response_header_.flags.rcode = query_context->response_code_;

  // Set the number of questions from the incoming query
  response_header_.questions = questions;

  // We will not include any additional records
  response_header_.authority_rrs = 0;
  response_header_.additional_rrs = 0;
}

void DnsMessageParser::buildDnsAnswerRecord(DnsQueryContextPtr& context,
                                            const DnsQueryRecord& query_rec,
                                            const std::chrono::seconds ttl,
                                            Network::Address::InstanceConstSharedPtr ipaddr) {
  // Verify that we have an address matching the query record type
  switch (query_rec.type_) {
  case DNS_RECORD_TYPE_AAAA:
    if (ipaddr->ip()->ipv6() == nullptr) {
      ENVOY_LOG(debug, "Unable to return IPV6 address for query");
      return;
    }
    break;

  case DNS_RECORD_TYPE_A:
    if (ipaddr->ip()->ipv4() == nullptr) {
      ENVOY_LOG(debug, "Unable to return IPV4 address for query");
      return;
    }
    break;

  // TODO(abbaptis): Support additional records (e.g. SRV)
  default:
    ENVOY_LOG(debug, "record type [{}] is not supported", query_rec.type_);
    return;
  }

  auto answer_record = std::make_unique<DnsAnswerRecord>(query_rec.name_, query_rec.type_,
                                                         query_rec.class_, ttl, std::move(ipaddr));
  context->answers_.emplace(query_rec.name_, std::move(answer_record));
}

void DnsMessageParser::setResponseCode(DnsQueryContextPtr& context,
                                       const uint16_t serialized_queries,
                                       const uint16_t serialized_answers) {
  // If the question is malformed, don't change the response
  if (context->response_code_ == DNS_RESPONSE_CODE_FORMAT_ERROR) {
    return;
  }
  // Check for unsupported request types
  for (const auto& query : context->queries_) {
    if (query->type_ != DNS_RECORD_TYPE_A && query->type_ != DNS_RECORD_TYPE_AAAA) {
      context->response_code_ = DNS_RESPONSE_CODE_NOT_IMPLEMENTED;
      return;
    }
  }
  // Output validation
  if (serialized_queries == 0) {
    context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    return;
  }
  if (serialized_answers == 0) {
    context->response_code_ = DNS_RESPONSE_CODE_NAME_ERROR;
    return;
  }
  context->response_code_ = DNS_RESPONSE_CODE_NO_ERROR;
}

void DnsMessageParser::buildResponseBuffer(DnsQueryContextPtr& query_context,
                                           Buffer::OwnedImpl& buffer) {
  // Ensure that responses stay below the 512 byte byte limit. If we are to exceed this we must add
  // DNS extension fields
  //
  // Note:  There is Network::MAX_UDP_PACKET_SIZE, which is defined as 1500 bytes. If we support
  // DNS extensions, which support up to 4096 bytes, we will have to keep this 1500 byte limit in
  // mind.
  static constexpr uint64_t MAX_DNS_RESPONSE_SIZE = 512;
  static constexpr uint64_t MAX_DNS_NAME_SIZE = 255;

  // Amazon Route53 will return up to 8 records in an answer
  // https://aws.amazon.com/route53/faqs/#associate_multiple_ip_with_single_record
  static constexpr size_t MAX_RETURNED_RECORDS = 8;

  // Each response must have DNS flags, which spans 4 bytes. Account for them immediately so that we
  // can adjust the number of returned answers to remain under the limit
  uint64_t total_buffer_size = sizeof(DnsHeaderFlags);
  uint16_t serialized_answers = 0;
  uint16_t serialized_queries = 0;

  Buffer::OwnedImpl query_buffer{};
  Buffer::OwnedImpl answer_buffer{};

  ENVOY_LOG(trace, "Building response for query ID [{}]", query_context->id_);

  for (const auto& query : query_context->queries_) {
    if (!query->serialize(query_buffer)) {
      ENVOY_LOG(debug, "Unable to serialize query record for {}", query->name_);
      continue;
    }

    // Serialize and account for each query's size. That said, there should be only one query.
    ++serialized_queries;
    total_buffer_size += query_buffer.length();

    const auto& answers = query_context->answers_;
    if (answers.empty()) {
      continue;
    }
    const size_t num_answers = answers.size();

    // Randomize the starting index if we have more than 8 records
    size_t index = num_answers > MAX_RETURNED_RECORDS ? rng_.random() % num_answers : 0;

    while (serialized_answers < num_answers) {
      const auto answer = std::next(answers.begin(), (index++ % num_answers));
      // Query names are limited to 255 characters. Since we are using ares to decode the encoded
      // names, we should not end up with a non-conforming name here.
      //
      // See Section 2.3.4 of https://tools.ietf.org/html/rfc1035
      if (query->name_.size() > MAX_DNS_NAME_SIZE) {
        query_context->counters_.record_name_overflow.inc();
        ENVOY_LOG(
            debug,
            "Query name '{}' is longer than the maximum permitted length. Skipping serialization",
            query->name_);
        continue;
      }
      if (answer->first != query->name_) {
        continue;
      }

      Buffer::OwnedImpl serialized_answer;
      if (!answer->second->serialize(serialized_answer)) {
        ENVOY_LOG(debug, "Unable to serialize answer record for {}", query->name_);
        continue;
      }
      const uint64_t serialized_answer_length = serialized_answer.length();
      if ((total_buffer_size + serialized_answer_length) > MAX_DNS_RESPONSE_SIZE) {
        break;
      }

      ++serialized_answers;
      total_buffer_size += serialized_answer_length;
      answer_buffer.add(serialized_answer);

      if (serialized_answers == MAX_RETURNED_RECORDS) {
        break;
      }
    }
  }

  setResponseCode(query_context, serialized_queries, serialized_answers);
  setDnsResponseFlags(query_context, serialized_queries, serialized_answers);

  // Build the response buffer for transmission to the client
  buffer.writeBEInt<uint16_t>(response_header_.id);

  uint16_t flags;
  ::memcpy(&flags, static_cast<void*>(&response_header_.flags), sizeof(uint16_t));
  buffer.writeBEInt<uint16_t>(flags);

  buffer.writeBEInt<uint16_t>(response_header_.questions);
  buffer.writeBEInt<uint16_t>(response_header_.answers);
  buffer.writeBEInt<uint16_t>(response_header_.authority_rrs);
  buffer.writeBEInt<uint16_t>(response_header_.additional_rrs);

  // write the queries and answers
  buffer.move(query_buffer);
  buffer.move(answer_buffer);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
