#pragma once

#include <sstream>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/kafka_protocol.h"
#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === VECTOR ==================================================================

template <typename T> std::ostream& operator<<(std::ostream& os, const std::vector<T>& arg) {
  os << "[";
  for (auto iter = arg.begin(); iter != arg.end(); iter++) {
    if (iter != arg.begin()) {
      os << ", ";
    }
    os << *iter;
  }
  os << "]";
  return os;
}

template <typename T> std::ostream& operator<<(std::ostream& os, const absl::optional<T>& arg) {
  if (arg.has_value()) {
    os << *arg;
  } else {
    os << "<null>";
  }
  return os;
}

// === REQUEST HEADER ==========================================================

struct RequestHeader {
  INT16 api_key_;
  INT16 api_version_;
  INT32 correlation_id_;
  NULLABLE_STRING client_id_;

  bool operator==(const RequestHeader& rhs) const {
    return api_key_ == rhs.api_key_ && api_version_ == rhs.api_version_ &&
           correlation_id_ == rhs.correlation_id_ && client_id_ == rhs.client_id_;
  };

  friend std::ostream& operator<<(std::ostream& os, const RequestHeader& arg) {
    return os << "{api_key=" << arg.api_key_ << ", api_version=" << arg.api_version_
              << ", correlation_id=" << arg.correlation_id_ << ", client_id=" << arg.client_id_
              << "}";
  };
};

struct RequestContext {
  INT32 remaining_request_size_{0};
  RequestHeader request_header_{};

  friend std::ostream& operator<<(std::ostream& os, const RequestContext& arg) {
    return os << "{header=" << arg.request_header_ << ", remaining=" << arg.remaining_request_size_
              << "}";
  }
};

typedef std::shared_ptr<RequestContext> RequestContextSharedPtr;

// === REQUEST PARSER MAPPING (REQUEST TYPE => PARSER) =========================

// a function generating a parser with given context
typedef std::function<ParserSharedPtr(RequestContextSharedPtr)> GeneratorFunction;

// two-level map: api_key -> api_version -> generator function
typedef std::unordered_map<INT16, std::shared_ptr<std::unordered_map<INT16, GeneratorFunction>>>
    GeneratorMap;

struct ParserSpec {
  const INT16 api_key_;
  const std::vector<INT16> api_versions_;
  const GeneratorFunction generator_;
};

// helper function that generates a map from specs looking like { api_key, api_versions... }
GeneratorMap computeGeneratorMap(std::vector<ParserSpec> arg);

/**
 * Provides the parser that is responsible for consuming the request-specific data
 * In other words: provides (api_key, api_version) -> Parser function
 */
class RequestParserResolver {
public:
  RequestParserResolver(std::vector<ParserSpec> arg) : generators_{computeGeneratorMap(arg)} {};
  virtual ~RequestParserResolver() = default;

  virtual ParserSharedPtr createParser(INT16 api_key, INT16 api_version,
                                       RequestContextSharedPtr context) const;

  static const RequestParserResolver KAFKA_0_11;

private:
  GeneratorMap generators_;
};

// === INITIAL PARSERS =========================================================

/**
 * Request start parser just consumes the length of request
 */
class RequestStartParser : public Parser {
public:
  RequestStartParser(const RequestParserResolver& parser_resolver)
      : parser_resolver_{parser_resolver}, context_{std::make_shared<RequestContext>()} {};

  ParseResponse parse(const char*& buffer, uint64_t& remaining);

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Buffer buffer_;
};

class RequestHeaderBuffer : public CompositeBuffer<RequestHeader, Int16Buffer, Int16Buffer,
                                                   Int32Buffer, NullableStringBuffer> {};

/**
 * Request header parser consumes request header
 */
class RequestHeaderParser : public Parser {
public:
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context)
      : parser_resolver_{parser_resolver}, context_{context} {};

  ParseResponse parse(const char*& buffer, uint64_t& remaining);

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderBuffer buffer_;
};

// === BUFFERED PARSER =========================================================

/**
 * Buffered parser uses a single buffer to construct a response
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message
 */
template <typename RT, typename BT> class BufferedParser : public Parser {
public:
  BufferedParser(RequestContextSharedPtr context) : context_{context} {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

protected:
  RequestContextSharedPtr context_;
  BT buffer_;
};

template <typename RT, typename BT>
ParseResponse BufferedParser<RT, BT>::parse(const char*& buffer, uint64_t& remaining) {
  context_->remaining_request_size_ -= buffer_.feed(buffer, remaining);
  if (buffer_.ready()) {
    // after a successful parse, there should be nothing left
    ASSERT(0 == context_->remaining_request_size_);
    RT request = buffer_.get();
    request.header() = context_->request_header_;
    ENVOY_LOG(trace, "parsed request {}: {}", *context_, request);
    MessageSharedPtr msg = std::make_shared<RT>(request);
    return ParseResponse::parsedMessage(msg);
  } else {
    return ParseResponse::stillWaiting();
  }
}

// names of Buffers/Parsers are influenced by org.apache.kafka.common.protocol.Protocol names

#define DEFINE_REQUEST_PARSER(REQUEST_TYPE, VERSION)                                               \
  class REQUEST_TYPE##VERSION##Parser                                                              \
      : public BufferedParser<REQUEST_TYPE, REQUEST_TYPE##VERSION##Buffer> {                       \
  public:                                                                                          \
    REQUEST_TYPE##VERSION##Parser(RequestContextSharedPtr ctx) : BufferedParser{ctx} {};           \
  };

// === ABSTRACT REQUEST ========================================================

class Request : public Message {
public:
  /**
   * Request header fields need to be initialized by user in case of newly created requests
   */
  Request(INT16 api_key) : request_header_{api_key, 0, 0, ""} {};

  Request(const RequestHeader& request_header) : request_header_{request_header} {};

  RequestHeader& header() { return request_header_; }

  INT16& apiVersion() { return request_header_.api_version_; }
  INT16 apiVersion() const { return request_header_.api_version_; }

  INT32& correlationId() { return request_header_.correlation_id_; }

  NULLABLE_STRING& clientId() { return request_header_.client_id_; }

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(request_header_.api_key_, dst);
    written += encoder.encode(request_header_.api_version_, dst);
    written += encoder.encode(request_header_.correlation_id_, dst);
    written += encoder.encode(request_header_.client_id_, dst);
    written += encodeDetails(dst, encoder);
    return written;
  }

  std::ostream& print(std::ostream& os) const override final {
    os << request_header_ << " "; // not very pretty
    return printDetails(os);
  }

protected:
  virtual size_t encodeDetails(Buffer::Instance&, EncodingContext&) const PURE;

  virtual std::ostream& printDetails(std::ostream&) const PURE;

  RequestHeader request_header_;
};

// === OFFSET COMMIT (8) =======================================================

struct OffsetCommitPartition {
  const INT32 partition_;
  const INT64 offset_;
  const INT64 timestamp_; // only v1
  const NULLABLE_STRING metadata_;

  // v0 *and* v2
  OffsetCommitPartition(INT32 partition, INT64 offset, NULLABLE_STRING metadata)
      : partition_{partition}, offset_{offset}, timestamp_{-1}, metadata_{metadata} {};

  // v1
  OffsetCommitPartition(INT32 partition, INT64 offset, INT64 timestamp, NULLABLE_STRING metadata)
      : partition_{partition}, offset_{offset}, timestamp_{timestamp}, metadata_{metadata} {};

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst);
    written += encoder.encode(offset_, dst);
    if (encoder.apiVersion() == 1) {
      written += encoder.encode(timestamp_, dst);
    }
    written += encoder.encode(metadata_, dst);
    return written;
  }

  bool operator==(const OffsetCommitPartition& rhs) const {
    return partition_ == rhs.partition_ && offset_ == rhs.offset_ && timestamp_ == rhs.timestamp_ &&
           metadata_ == rhs.metadata_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetCommitPartition& arg) {
    return os << "{partition=" << arg.partition_ << ", offset=" << arg.offset_
              << ", timestamp=" << arg.timestamp_ << ", metadata=" << arg.metadata_ << "}";
  }
};

struct OffsetCommitTopic {
  const STRING topic_;
  const NULLABLE_ARRAY<OffsetCommitPartition> partitions_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  bool operator==(const OffsetCommitTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetCommitTopic& arg) {
    return os << "{topic=" << arg.topic_ << ", partitions_=" << arg.partitions_ << "}";
  }
};

class OffsetCommitRequest : public Request {
public:
  // v0
  OffsetCommitRequest(STRING group_id, NULLABLE_ARRAY<OffsetCommitTopic> topics)
      : OffsetCommitRequest(group_id, -1, "", -1, topics){};

  // v1
  OffsetCommitRequest(STRING group_id, INT32 group_generation_id, STRING member_id,
                      NULLABLE_ARRAY<OffsetCommitTopic> topics)
      : OffsetCommitRequest(group_id, group_generation_id, member_id, -1, topics){};

  // v2 .. v3
  OffsetCommitRequest(STRING group_id, INT32 group_generation_id, STRING member_id,
                      INT64 retention_time, NULLABLE_ARRAY<OffsetCommitTopic> topics)
      : Request{RequestType::OffsetCommit}, group_id_{group_id},
        group_generation_id_{group_generation_id}, member_id_{member_id},
        retention_time_{retention_time}, topics_{topics} {};

  bool operator==(const OffsetCommitRequest& rhs) const {
    return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ &&
           group_generation_id_ == rhs.group_generation_id_ && member_id_ == rhs.member_id_ &&
           retention_time_ == rhs.retention_time_ && topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(group_id_, dst);
    if (encoder.apiVersion() >= 1) {
      written += encoder.encode(group_generation_id_, dst);
      written += encoder.encode(member_id_, dst);
    }
    if (encoder.apiVersion() >= 2) {
      written += encoder.encode(retention_time_, dst);
    }
    written += encoder.encode(topics_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{group_id=" << group_id_ << ", group_generation_id=" << group_generation_id_
              << ", member_id=" << member_id_ << ", retention_time=" << retention_time_
              << ", topics=" << topics_ << "}";
  }

private:
  const STRING group_id_;
  const INT32 group_generation_id_; // since v1
  const STRING member_id_;          // since v1
  const INT64 retention_time_;      // since v2
  const NULLABLE_ARRAY<OffsetCommitTopic> topics_;
};

// clang-format off
class OffsetCommitPartitionV0Buffer : public CompositeBuffer<OffsetCommitPartition, Int32Buffer, Int64Buffer, NullableStringBuffer> {};
class OffsetCommitPartitionV0ArrayBuffer : public ArrayBuffer<OffsetCommitPartition, OffsetCommitPartitionV0Buffer> {};
class OffsetCommitTopicV0Buffer : public CompositeBuffer<OffsetCommitTopic, StringBuffer, OffsetCommitPartitionV0ArrayBuffer> {};
class OffsetCommitTopicV0ArrayBuffer : public ArrayBuffer<OffsetCommitTopic, OffsetCommitTopicV0Buffer> {};

class OffsetCommitPartitionV1Buffer : public CompositeBuffer<OffsetCommitPartition, Int32Buffer, Int64Buffer, Int64Buffer, NullableStringBuffer> {};
class OffsetCommitPartitionV1ArrayBuffer : public ArrayBuffer<OffsetCommitPartition, OffsetCommitPartitionV1Buffer> {};
class OffsetCommitTopicV1Buffer : public CompositeBuffer<OffsetCommitTopic, StringBuffer, OffsetCommitPartitionV1ArrayBuffer> {};
class OffsetCommitTopicV1ArrayBuffer : public ArrayBuffer<OffsetCommitTopic, OffsetCommitTopicV1Buffer> {};

class OffsetCommitRequestV0Buffer : public CompositeBuffer<OffsetCommitRequest, StringBuffer, OffsetCommitTopicV0ArrayBuffer> {};
class OffsetCommitRequestV1Buffer : public CompositeBuffer<OffsetCommitRequest, StringBuffer, Int32Buffer, StringBuffer, OffsetCommitTopicV1ArrayBuffer> {};

DEFINE_REQUEST_PARSER(OffsetCommitRequest, V0);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V1);
// clang-format on

// === UNKNOWN REQUEST =========================================================

class UnknownRequest : public Request {
public:
  UnknownRequest(const RequestHeader& request_header) : Request{request_header} {};

protected:
  // this isn't the prettiest, as we have thrown away the data
  // XXX(adam.kotwasinski) discuss capturing the data as-is, and simply putting it back
  //   this would add ability to forward unknown types of requests in cluster-proxy
  size_t encodeDetails(Buffer::Instance&, EncodingContext&) const override {
    throw EnvoyException("cannot serialize unknown request");
  }

  std::ostream& printDetails(std::ostream& out) const override {
    return out << "{unknown request}";
  }
};

// ignores data until the end of request (contained in context_)
class SentinelConsumer : public Parser {
public:
  SentinelConsumer(RequestContextSharedPtr context) : context_{context} {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestContextSharedPtr context_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
