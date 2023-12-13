#include "dns_filter_test_utils.h"

#include "source/common/common/random_generator.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "test/test_common/utility.h"

#include "ares.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

std::string buildQueryFromBytes(const char* bytes, const size_t count) {
  std::string query;
  for (size_t i = 0; i < count; i++) {
    query.append(static_cast<const char*>(&bytes[i]), 1);
  }
  return query;
}

std::string buildQueryForDomain(const std::string& name, uint16_t rec_type, uint16_t rec_class,
                                const uint16_t query_id) {
  Random::RandomGeneratorImpl random_;
  struct DnsHeader query {};
  uint16_t id = query_id ? query_id : (random_.random() % 0xFFFF) + 1;

  // Generate a random query ID
  query.id = id;

  // Signify that this is a query
  query.flags.qr = 0;

  // This should usually be zero
  query.flags.opcode = 0;

  query.flags.aa = 0;
  query.flags.tc = 0;

  // Set Recursion flags (at least one bit set so that the flags are not all zero)
  query.flags.rd = 1;
  query.flags.ra = 0;

  // reserved flag is not set
  query.flags.z = 0;

  // Set the authenticated flags to zero
  query.flags.ad = 0;
  query.flags.cd = 0;

  query.questions = 1;
  query.answers = 0;
  query.authority_rrs = 0;
  query.additional_rrs = 0;

  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint16_t>(query.id);

  uint16_t flags;
  ::memcpy(&flags, static_cast<void*>(&query.flags), sizeof(uint16_t));
  buffer.writeBEInt<uint16_t>(flags);

  buffer.writeBEInt<uint16_t>(query.questions);
  buffer.writeBEInt<uint16_t>(query.answers);
  buffer.writeBEInt<uint16_t>(query.authority_rrs);
  buffer.writeBEInt<uint16_t>(query.additional_rrs);

  DnsQueryRecord query_rec(name, rec_type, rec_class);
  query_rec.serialize(buffer);
  return buffer.toString();
}

void verifyAddress(const std::list<std::string>& addresses, const DnsAnswerRecordPtr& answer) {
  ASSERT_TRUE(answer != nullptr);
  ASSERT_TRUE(answer->ip_addr_ != nullptr);

  const auto resolved_address = answer->ip_addr_->ip()->addressAsString();
  if (addresses.size() == 1) {
    const auto expected = addresses.begin();
    ASSERT_EQ(*expected, resolved_address);
    return;
  }

  const auto iter = std::find(addresses.begin(), addresses.end(), resolved_address);
  ASSERT_TRUE(iter != addresses.end());
}

absl::string_view getProtoFromName(const absl::string_view name) {
  size_t start = name.find_first_of('.');
  if (start != std::string::npos && ++start < name.size() - 1) {
    if (name[start] == '_') {
      const size_t offset = name.find_first_of('.', ++start);
      if (start != std::string::npos && offset < name.size()) {
        return name.substr(start, offset - start);
      }
    }
  }
  return EMPTY_STRING;
}

DnsQueryContextPtr DnsResponseValidator::createResponseContext(Network::UdpRecvData& client_request,
                                                               DnsParserCounters& counters) {
  DnsQueryContextPtr query_context = std::make_unique<DnsQueryContext>(
      client_request.addresses_.local_, client_request.addresses_.peer_, counters, 1);

  // TODO(boteng): The response parse can be replaced by c-ares methods like: ares_parse_a_reply
  query_context->parse_status_ = validateDnsResponseObject(query_context, client_request.buffer_);
  if (!query_context->parse_status_) {
    query_context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    ENVOY_LOG(debug, "Unable to parse query buffer from '{}' into a DNS object",
              client_request.addresses_.peer_->ip()->addressAsString());
  }
  return query_context;
}

bool DnsResponseValidator::validateDnsResponseObject(DnsQueryContextPtr& context,
                                                     const Buffer::InstancePtr& buffer) {
  static constexpr uint64_t field_size = sizeof(uint16_t);
  size_t available_bytes = buffer->length();
  uint64_t offset = 0;
  uint16_t data;
  bool done = false;
  DnsQueryParseState state{DnsQueryParseState::Init};

  context->header_ = {};
  do {
    // Ensure that we have enough data remaining in the buffer to parse the query
    if (available_bytes < field_size) {
      context->counters_.underflow_counter.inc();
      ENVOY_LOG(debug,
                "Exhausted available bytes in the buffer. Insufficient data to parse query field.");
      return false;
    }

    // Each aggregate DNS header field is 2 bytes wide.
    data = buffer->peekBEInt<uint16_t>(offset); // pop bytes from here
    offset += field_size;
    available_bytes -= field_size;

    switch (state) {
    case DnsQueryParseState::Init:
      context->header_.id = data;
      state = DnsQueryParseState::Flags;
      break;
    case DnsQueryParseState::Flags:
      safeMemcpyUnsafeDst(static_cast<void*>(&context->header_.flags), &data);
      state = DnsQueryParseState::Questions;
      break;
    case DnsQueryParseState::Questions:
      context->header_.questions = data;
      state = DnsQueryParseState::Answers;
      break;
    case DnsQueryParseState::Answers:
      context->header_.answers = data;
      state = DnsQueryParseState::Authority;
      break;
    case DnsQueryParseState::Authority:
      context->header_.authority_rrs = data;
      state = DnsQueryParseState::Authority2;
      break;
    case DnsQueryParseState::Authority2:
      context->header_.additional_rrs = data;
      done = true;
      break;
    }
  } while (!done);

  // Only response is parsed and verified
  if (context->header_.flags.qr != 1) {
    ENVOY_LOG(debug, "Is not a DNS response");
    return false;
  }

  if (context->header_.questions != 1) {
    context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    ENVOY_LOG(debug, "Unexpected number [{}] of questions in DNS query",
              static_cast<int>(context->header_.questions));
    return false;
  }

  context->id_ = static_cast<uint16_t>(context->header_.id);
  if (context->id_ == 0) {
    ENVOY_LOG(debug, "No ID in DNS Response");
    return false;
  }

  // Almost always, we will have only one query here. Per the RFC, QDCOUNT is usually 1
  context->queries_.reserve(context->header_.questions);
  for (auto index = 0; index < context->header_.questions; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] questions", index,
              static_cast<int>(context->header_.questions));

    const std::string record_name = parseDnsNameRecord(buffer, available_bytes, offset);
    // Read the record type
    uint16_t record_type;
    record_type = buffer->peekBEInt<uint16_t>(offset);
    offset += sizeof(uint16_t);

    // Read the record class. This value is always 1 for internet address records
    uint16_t record_class;
    record_class = buffer->peekBEInt<uint16_t>(offset);
    offset += sizeof(record_class);

    auto rec = std::make_unique<DnsQueryRecord>(record_name, record_type, record_class);
    context->queries_.push_back(std::move(rec));
  }

  // From RFC 1035
  // 4.1.3. Resource record format
  //
  // The answer, authority, and additional sections all share the same format: a variable number of
  // resource records, where the number of records is specified in the corresponding count field in
  // the header.
  if (context->header_.answers &&
      !parseAnswerRecords(context->answers_, context->header_.answers, buffer, offset)) {
    return false;
  }

  if (context->header_.authority_rrs) {
    // We are not generating these in the filter and don't have a use for them at the moment.
    ENVOY_LOG(debug, "Authority RRs are not supported");
    return false;
  }

  if (context->header_.additional_rrs) {
    // We may encounter additional resource records that we do not support. Since the filter
    // operates on queries, we can skip any additional records that we cannot parse since
    // they will not affect responses.
    parseAnswerRecords(context->additional_, context->header_.additional_rrs, buffer, offset);
  }

  return true;
}

bool DnsResponseValidator::parseAnswerRecords(DnsAnswerMap& answers, const uint16_t answer_count,
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

DnsAnswerRecordPtr DnsResponseValidator::parseDnsARecord(DnsAnswerCtx& ctx) {
  Network::Address::InstanceConstSharedPtr ip_addr = nullptr;

  switch (ctx.record_type_) {
  case DNS_RECORD_TYPE_A:
    if (ctx.available_bytes_ >= sizeof(uint32_t)) {
      sockaddr_in sa4;
      memset(&sa4, 0, sizeof(sa4));
      sa4.sin_addr.s_addr = ctx.buffer_->peekLEInt<uint32_t>(ctx.offset_);
      ip_addr = std::make_shared<Network::Address::Ipv4Instance>(&sa4);
      ctx.offset_ += ctx.data_length_;
    }
    break;
  case DNS_RECORD_TYPE_AAAA:
    if (ctx.available_bytes_ >= sizeof(absl::uint128)) {
      sockaddr_in6 sa6;
      memset(&sa6, 0, sizeof(sa6));
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

DnsSrvRecordPtr DnsResponseValidator::parseDnsSrvRecord(DnsAnswerCtx& ctx) {
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
  const absl::string_view proto = getProtoFromName(ctx.record_name_);

  if (!proto.empty() && !target_name.empty()) {
    auto srv_record =
        std::make_unique<DnsSrvRecord>(ctx.record_name_, proto, std::chrono::seconds(ctx.ttl_));
    srv_record->addTarget(target_name, attrs);
    return srv_record;
  }
  return nullptr;
}

const std::string DnsResponseValidator::parseDnsNameRecord(const Buffer::InstancePtr& buffer,
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

DnsAnswerRecordPtr DnsResponseValidator::parseDnsAnswerRecord(const Buffer::InstancePtr& buffer,
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

  // TODO(suniltheta): Support Extension Mechanisms for DNS (RFC2671)
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

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
