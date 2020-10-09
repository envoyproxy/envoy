#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include "envoy/network/address.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/udp/dns_filter/dns_filter_utils.h"

#include "ares.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

bool BaseDnsRecord::serializeSpecificName(Buffer::OwnedImpl& output, const absl::string_view name) {
  // Iterate over a name e.g. "www.domain.com" once and produce a buffer containing each name
  // segment prefixed by its length
  static constexpr char SEPARATOR = '.';

  // Names are restricted to 255 bytes per RFC
  if (name.size() > MAX_NAME_LENGTH) {
    return false;
  }

  size_t last = 0;
  size_t count = name.find_first_of(SEPARATOR);
  auto iter = name.begin();

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
    count = name.find_first_of(SEPARATOR, ++last);
  }

  // Write the remaining segment prepended by its length
  count = name.size() - last;
  output.writeBEInt<uint8_t>(count);
  for (size_t i = 0; i < count; i++) {
    output.writeByte(*iter++);
  }

  // Terminate the name record with a null byte
  output.writeByte(0x00);
  return true;
}

bool BaseDnsRecord::serializeName(Buffer::OwnedImpl& output) {
  return serializeSpecificName(output, name_);
}

// Serialize a DNS Query Record
bool DnsQueryRecord::serialize(Buffer::OwnedImpl& output) {
  if (serializeName(output)) {
    output.writeBEInt<uint16_t>(type_);
    output.writeBEInt<uint16_t>(class_);
  }
  return (output.length() > 0);
}

// Serialize a single DNS Answer Record
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
  }
  return (output.length() > 0);
}

bool DnsSrvRecord::serialize(Buffer::OwnedImpl& output) {
  if (!targets_.empty()) {
    // The Service Record being serialized should have only one target
    const auto& target = targets_.begin();
    Buffer::OwnedImpl target_buf{};
    if (serializeSpecificName(target_buf, target->first) && serializeName(output)) {
      output.writeBEInt<uint16_t>(type_);
      output.writeBEInt<uint16_t>(class_);
      output.writeBEInt<uint32_t>(static_cast<uint32_t>(ttl_.count()));

      const uint16_t data_length = sizeof(target->second.priority) + sizeof(target->second.weight) +
                                   sizeof(target->second.port) + target_buf.length();
      output.writeBEInt<uint16_t>(data_length);
      output.writeBEInt<uint16_t>(target->second.priority);
      output.writeBEInt<uint16_t>(target->second.weight);
      output.writeBEInt<uint16_t>(target->second.port);
      output.move(target_buf);
    }
  }
  return (output.length() > 0);
}

void DnsSrvRecord::addTarget(const absl::string_view target, const DnsTargetAttributes& attrs) {
  targets_.emplace(std::make_pair(std::string(target), attrs));
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
  bool done = false;
  DnsQueryParseState state{DnsQueryParseState::Init};

  header_ = {};
  do {
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

    switch (state) {
    case DnsQueryParseState::Init:
      header_.id = data;
      state = DnsQueryParseState::Flags;
      break;
    case DnsQueryParseState::Flags:
      ::memcpy(static_cast<void*>(&header_.flags), &data, field_size);
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
      done = true;
      break;
    }
  } while (!done);

  if (!header_.flags.qr && header_.answers) {
    ENVOY_LOG(debug, "Answer records present in query");
    return false;
  }

  if (header_.questions != 1) {
    context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    ENVOY_LOG(debug, "Unexpected number [{}] of questions in DNS query", header_.questions);
    return false;
  }

  context->id_ = static_cast<uint16_t>(header_.id);
  if (context->id_ == 0) {
    ENVOY_LOG(debug, "No ID in DNS query");
    return false;
  }

  // Almost always, we will have only one query here. Per the RFC, QDCOUNT is usually 1
  context->queries_.reserve(header_.questions);
  for (auto index = 0; index < header_.questions; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] questions", index, header_.questions);
    auto rec = parseDnsQueryRecord(buffer, offset);
    if (rec == nullptr) {
      context->counters_.query_parsing_failure.inc();
      ENVOY_LOG(debug, "Couldn't parse query record from buffer");
      return false;
    }
    context->queries_.push_back(std::move(rec));
  }

  // From RFC 1035
  // 4.1.3. Resource record format
  //
  // The answer, authority, and additional sections all share the same format: a variable number of
  // resource records, where the number of records is specified in the corresponding count field in
  // the header.

  // Parse Answer Records and Additional Resource Records. This is primarily used for tests
  // to validate the response generated by the filter
  if (header_.answers && !parseAnswerRecords(context->answers_, header_.answers, buffer, offset)) {
    return false;
  }

  if (header_.authority_rrs) {
    // We are not generating these in the filter and don't have a use for them at the moment.
    // If they exist, we will not parse them and return an error to the client since they appear
    // between the answers and additional resource records in the buffer. We return true so that
    // the proper status code is sent to the client
    context->response_code_ = DNS_RESPONSE_CODE_NOT_IMPLEMENTED;
    return true;
  }

  if (header_.additional_rrs) {
    // We may encounter additional resource records that we do not support. Since the filter
    // operates on queries, we can skip any additional records that we cannot parse since
    // they will not affect responses.
    parseAnswerRecords(context->additional_, header_.additional_rrs, buffer, offset);
  }

  return true;
}

bool DnsMessageParser::parseAnswerRecords(DnsAnswerMap& answers, const uint16_t answer_count,
                                          const Buffer::InstancePtr& buffer, uint64_t& offset) {
  answers.reserve(answer_count);
  for (auto index = 0; index < answer_count; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] answers", index, answer_count);
    auto rec = parseDnsAnswerRecord(buffer, offset);
    if (rec == nullptr) {
      ENVOY_LOG(debug, "Couldn't parse answer record from buffer");
      return false;
    }
    const std::string name = rec->name_;
    answers.emplace(name, std::move(rec));
  }
  return true;
}

const std::string DnsMessageParser::parseDnsNameRecord(const Buffer::InstancePtr& buffer,
                                                       uint64_t& available_bytes,
                                                       uint64_t& name_offset) {
  void* buf = buffer->linearize(static_cast<uint32_t>(buffer->length()));
  const unsigned char* linearized_data = static_cast<const unsigned char*>(buf);
  const unsigned char* record = linearized_data + name_offset;
  long encoded_len;
  char* output;

  const int result =
      ares_expand_name(record, linearized_data, buffer->length(), &output, &encoded_len);
  if (result != ARES_SUCCESS) {
    return EMPTY_STRING;
  }

  std::string name(output);
  ares_free_string(output);
  name_offset += encoded_len;
  available_bytes -= encoded_len;

  return name;
}

DnsAnswerRecordPtr DnsMessageParser::parseDnsARecord(DnsAnswerCtx& ctx) {
  Network::Address::InstanceConstSharedPtr ip_addr = nullptr;

  switch (ctx.record_type_) {
  case DNS_RECORD_TYPE_A:
    if (ctx.available_bytes_ >= sizeof(uint32_t)) {
      sockaddr_in sa4;
      sa4.sin_addr.s_addr = ctx.buffer_->peekLEInt<uint32_t>(ctx.offset_);
      ip_addr = std::make_shared<Network::Address::Ipv4Instance>(&sa4);
      ctx.offset_ += ctx.data_length_;
    }
    break;
  case DNS_RECORD_TYPE_AAAA:
    if (ctx.available_bytes_ >= sizeof(absl::uint128)) {
      sockaddr_in6 sa6;
      uint8_t* address6_bytes = reinterpret_cast<uint8_t*>(&sa6.sin6_addr.s6_addr);
      static constexpr size_t count = sizeof(absl::uint128) / sizeof(uint8_t);
      for (size_t index = 0; index < count; index++) {
        *address6_bytes++ = ctx.buffer_->peekLEInt<uint8_t>(ctx.offset_++);
      }
      ip_addr = std::make_shared<Network::Address::Ipv6Instance>(sa6, true);
    }
    break;
  }

  if (ip_addr == nullptr) {
    ENVOY_LOG(debug, "No IP parsed from an A or AAAA record");
    return nullptr;
  }

  ENVOY_LOG(trace, "Parsed address [{}] from record type [{}]: offset {}",
            ip_addr->ip()->addressAsString(), ctx.record_type_, ctx.offset_);

  return std::make_unique<DnsAnswerRecord>(ctx.record_name_, ctx.record_type_, ctx.record_class_,
                                           std::chrono::seconds(ctx.ttl_), std::move(ip_addr));
}

DnsSrvRecordPtr DnsMessageParser::parseDnsSrvRecord(DnsAnswerCtx& ctx) {
  uint64_t data_length = ctx.data_length_;

  if (data_length < 3 * sizeof(uint16_t)) {
    ENVOY_LOG(debug, "Insufficient data for reading a complete SRV answer record");
    return nullptr;
  }

  uint64_t available_bytes = ctx.buffer_->length() - ctx.offset_;
  if (available_bytes < data_length) {
    ENVOY_LOG(debug, "No data left in buffer for reading SRV answer record");
    return nullptr;
  }

  DnsSrvRecord::DnsTargetAttributes attrs{};
  attrs.priority = ctx.buffer_->peekBEInt<uint16_t>(ctx.offset_);
  ctx.offset_ += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  attrs.weight = ctx.buffer_->peekBEInt<uint16_t>(ctx.offset_);
  ctx.offset_ += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  attrs.port = ctx.buffer_->peekBEInt<uint16_t>(ctx.offset_);
  ctx.offset_ += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  const std::string target_name = parseDnsNameRecord(ctx.buffer_, available_bytes, ctx.offset_);
  const absl::string_view proto = Utils::getProtoFromName(ctx.record_name_);

  if (!proto.empty() && !target_name.empty()) {
    auto srv_record =
        std::make_unique<DnsSrvRecord>(ctx.record_name_, proto, std::chrono::seconds(ctx.ttl_));
    srv_record->addTarget(target_name, attrs);
    return srv_record;
  }
  return nullptr;
}

DnsAnswerRecordPtr DnsMessageParser::parseDnsAnswerRecord(const Buffer::InstancePtr& buffer,
                                                          uint64_t& offset) {
  uint64_t available_bytes = buffer->length() - offset;
  const std::string record_name = parseDnsNameRecord(buffer, available_bytes, offset);
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
  record_type = buffer->peekBEInt<uint16_t>(offset);
  offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  // TODO(abaptiste): Support Extension Mechanisms for DNS (RFC2671)
  //
  // We may see optional records indicating DNS extension support. We need to skip
  // these records until we have proper support. Encountering one of these records
  // does not indicate a failure. We support A, AAAA and SRV record types
  if (record_type != DNS_RECORD_TYPE_A && record_type != DNS_RECORD_TYPE_AAAA &&
      record_type != DNS_RECORD_TYPE_SRV) {
    ENVOY_LOG(debug, "Unsupported record type [{}] found in answer", record_type);
    return nullptr;
  }

  // Parse the record class
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(offset);
  offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  // We support only IN record classes
  if (record_class != DNS_RECORD_CLASS_IN) {
    ENVOY_LOG(debug, "Unsupported record class [{}] found in answer", record_class);
    return nullptr;
  }

  // Read the record's TTL
  uint32_t ttl;
  ttl = buffer->peekBEInt<uint32_t>(offset);
  offset += sizeof(uint32_t);
  available_bytes -= sizeof(uint32_t);

  // Parse the Data Length and address data record
  uint16_t data_length;
  data_length = buffer->peekBEInt<uint16_t>(offset);
  offset += sizeof(uint16_t);
  available_bytes -= sizeof(uint16_t);

  if (data_length == 0) {
    ENVOY_LOG(debug, "Read zero for data length when reading address from answer record");
    return nullptr;
  }

  auto ctx = DnsAnswerCtx(buffer, record_name, record_type, record_class, available_bytes,
                          data_length, ttl, offset);

  switch (record_type) {
  case DNS_RECORD_TYPE_A:
  case DNS_RECORD_TYPE_AAAA:
    return parseDnsARecord(ctx);
  case DNS_RECORD_TYPE_SRV:
    return parseDnsSrvRecord(ctx);
  default:
    ENVOY_LOG(debug, "Unsupported record type [{}] found in answer", record_type);
    return nullptr;
  }
}

DnsQueryRecordPtr DnsMessageParser::parseDnsQueryRecord(const Buffer::InstancePtr& buffer,
                                                        uint64_t& offset) {
  uint64_t available_bytes = buffer->length() - offset;

  // This is the minimum data length needed to parse a name [length, value, null byte]
  if (available_bytes < MIN_QUERY_NAME_LENGTH) {
    ENVOY_LOG(debug, "No available data in buffer to parse a query record");
    return nullptr;
  }

  const std::string record_name = parseDnsNameRecord(buffer, available_bytes, offset);
  if (record_name.empty()) {
    ENVOY_LOG(debug, "Unable to parse name record from buffer [length {}]", buffer->length());
    return nullptr;
  }

  // After reading the name we should have data for the record type and class
  if (available_bytes < 2 * sizeof(uint16_t)) {
    ENVOY_LOG(debug,
              "Insufficient data in buffer to read query record type and class. "
              "Available bytes: {}",
              available_bytes);
    return nullptr;
  }

  // Read the record type
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(offset);
  offset += sizeof(record_type);

  // Read the record class. This value is always 1 for internet address records
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(offset);
  offset += sizeof(record_class);

  if (record_class != DNS_RECORD_CLASS_IN) {
    ENVOY_LOG(debug, "Unsupported record class '{}' in address record", record_class);
    return nullptr;
  }

  auto rec = std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);
  rec->query_time_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      query_latency_histogram_, timesource_);

  ENVOY_LOG(trace, "Extracted query record. Name: {} type: {} class: {}", record_name, record_type,
            record_class);

  return rec;
}

void DnsMessageParser::setDnsResponseFlags(DnsQueryContextPtr& query_context,
                                           const uint16_t questions, const uint16_t answers,
                                           const uint16_t authority_rrs,
                                           const uint16_t additional_rrs) {
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

  response_header_.authority_rrs = authority_rrs;
  response_header_.additional_rrs = additional_rrs;
}

bool DnsMessageParser::createAndStoreDnsAnswerRecord(
    const absl::string_view name, const uint16_t rec_type, const uint16_t rec_class,
    const std::chrono::seconds ttl, Network::Address::InstanceConstSharedPtr ipaddr,
    DnsAnswerMap& collection) {
  // Verify that we have an address matching the query record type
  switch (rec_type) {
  case DNS_RECORD_TYPE_AAAA:
    if (ipaddr->ip()->ipv6() == nullptr) {
      ENVOY_LOG(debug, "Unable to return IPV6 address for query");
      return false;
    }
    break;

  case DNS_RECORD_TYPE_A:
    if (ipaddr->ip()->ipv4() == nullptr) {
      ENVOY_LOG(debug, "Unable to return IPV4 address for query");
      return false;
    }
    break;
  }

  auto answer_record =
      std::make_unique<DnsAnswerRecord>(name, rec_type, rec_class, ttl, std::move(ipaddr));
  collection.emplace(std::string(name), std::move(answer_record));

  return true;
}

bool DnsMessageParser::storeDnsAdditionalRecord(DnsQueryContextPtr& context,
                                                const absl::string_view name,
                                                const uint16_t rec_type, const uint16_t rec_class,
                                                const std::chrono::seconds ttl,
                                                Network::Address::InstanceConstSharedPtr ipaddr) {
  return createAndStoreDnsAnswerRecord(name, rec_type, rec_class, ttl, std::move(ipaddr),
                                       context->additional_);
}

bool DnsMessageParser::storeDnsAnswerRecord(DnsQueryContextPtr& context,
                                            const DnsQueryRecord& query_rec,
                                            const std::chrono::seconds ttl,
                                            Network::Address::InstanceConstSharedPtr ipaddr) {
  return createAndStoreDnsAnswerRecord(query_rec.name_, query_rec.type_, query_rec.class_, ttl,
                                       std::move(ipaddr), context->answers_);
}

void DnsMessageParser::addNewDnsSrvAnswerRecord(DnsQueryContextPtr& context,
                                                const DnsQueryRecord& query_rec,
                                                DnsSrvRecordPtr service) {
  RELEASE_ASSERT(query_rec.class_ == DNS_RECORD_CLASS_IN, "Unsupported DNS Record Class in record");
  if (query_rec.type_ == DNS_RECORD_TYPE_SRV) {
    context->answers_.emplace(query_rec.name_, std::move(service));
  }
}

void DnsMessageParser::storeDnsSrvAnswerRecord(DnsQueryContextPtr& context,
                                               const DnsQueryRecord& query_rec,
                                               const DnsSrvRecordPtr& service) {
  if (query_rec.type_ == DNS_RECORD_TYPE_SRV) {
    ENVOY_LOG(trace, "storing answer record type [{}] for {}", query_rec.type_, query_rec.name_);

    auto srv_record = std::make_unique<DnsSrvRecord>(*service);
    addNewDnsSrvAnswerRecord(context, query_rec, std::move(srv_record));
  }
}

void DnsMessageParser::setResponseCode(DnsQueryContextPtr& context,
                                       const uint16_t serialized_queries,
                                       const uint16_t serialized_answers) {
  // Do not change the response returned to the client if the following errors have
  // already been set
  if (context->response_code_ == DNS_RESPONSE_CODE_FORMAT_ERROR ||
      context->response_code_ == DNS_RESPONSE_CODE_NOT_IMPLEMENTED) {
    return;
  }

  // Check for unsupported request types
  for (const auto& query : context->queries_) {
    switch (query->type_) {
    case DNS_RECORD_TYPE_A:
    case DNS_RECORD_TYPE_AAAA:
    case DNS_RECORD_TYPE_SRV:
      break;
    default:
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
  // Each response must have DNS flags, which spans 4 bytes. Account for them immediately so
  // that we can adjust the number of returned answers to remain under the limit
  size_t total_buffer_size = sizeof(DnsHeaderFlags);
  uint16_t touched_answers = 0;
  uint16_t serialized_answers = 0;
  uint16_t serialized_queries = 0;
  uint16_t serialized_authority_rrs = 0;
  uint16_t serialized_additional_rrs = 0;

  Buffer::OwnedImpl query_buffer{};
  Buffer::OwnedImpl answer_buffer{};
  Buffer::OwnedImpl addl_rec_buffer{};

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

    // Serialize the additional records in parallel with the answers to ensure consistent
    // records
    const auto& additional_rrs = query_context->additional_;

    const size_t num_answers = answers.size();

    // Randomize the starting index if we have more than 8 records
    size_t index = num_answers > MAX_RETURNED_RECORDS ? rng_.random() % num_answers : 0;
    while (serialized_answers < num_answers && touched_answers < num_answers) {
      const auto answer = std::next(answers.begin(), (index++ % num_answers));
      ++touched_answers;

      // Query names are limited to 255 characters. Since we are using c-ares to decode the
      // encoded query names, we should not end up with a non-conforming name here.
      //
      // See Section 2.3.4 of https://tools.ietf.org/html/rfc1035
      RELEASE_ASSERT(query->name_.size() < MAX_NAME_LENGTH,
                     "Query name is too large for serialization");

      // Serialize answer records whose names and types match the query
      if (answer->first == query->name_ && answer->second->type_ == query->type_) {
        // Ensure that we can serialize the answer and the corresponding SRV additional
        // record together.

        // It is still possible that there may be more additional records than those referenced
        // by the answers. However, each serialized answer will have an accompanying additional
        // record for the host.
        if (query->type_ == DNS_RECORD_TYPE_SRV) {
          const DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer->second.get());
          const auto& target = srv_rec->targets_.begin();
          const auto& rr = additional_rrs.find(target->first);

          if (rr != additional_rrs.end()) {
            Buffer::OwnedImpl serialized_rr{};

            // If serializing the additional record fails, skip serializing the answer record
            if (!rr->second->serialize(serialized_rr)) {
              ENVOY_LOG(debug, "Unable to serialize answer record for {}", query->name_);
              continue;
            }
            total_buffer_size += serialized_rr.length();
            addl_rec_buffer.add(serialized_rr);
            ++serialized_additional_rrs;
          }
        }

        // Now we serialize the answer record. We check the length of the serialized
        // data to ensure we don't exceed the DNS response limit
        Buffer::OwnedImpl serialized_answer;
        if (!answer->second->serialize(serialized_answer)) {
          ENVOY_LOG(debug, "Unable to serialize answer record for {}", query->name_);
          continue;
        }
        total_buffer_size += serialized_answer.length();
        if (total_buffer_size > MAX_DNS_RESPONSE_SIZE) {
          break;
        }
        answer_buffer.add(serialized_answer);
        if (++serialized_answers == MAX_RETURNED_RECORDS) {
          break;
        }
      }
    }
  }

  setResponseCode(query_context, serialized_queries, serialized_answers);
  setDnsResponseFlags(query_context, serialized_queries, serialized_answers,
                      serialized_authority_rrs, serialized_additional_rrs);

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
  buffer.move(addl_rec_buffer);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
