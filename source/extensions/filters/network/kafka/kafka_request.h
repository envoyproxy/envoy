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

// === REQUEST =================================================================

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
    return os << "{api_key = " << arg.api_key_ << ", api_version = " << arg.api_version_
              << ", correlation_id = " << arg.correlation_id_ << ", client_id = " << arg.client_id_
              << "}";
  };
};

struct RequestContext {
  INT32 remaining_request_size_{0};
  RequestHeader request_header_{};

  friend std::ostream& operator<<(std::ostream& os, const RequestContext& arg) {
    return os << "{header = " << arg.request_header_
              << ", remaining = " << arg.remaining_request_size_ << "}";
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
 * Responsible for mapping [apiKey, apiVersion] -> request parser
 */
class RequestParserResolver {
public:
  RequestParserResolver(std::vector<ParserSpec> arg) : generators_{computeGeneratorMap(arg)} {};

  ParserSharedPtr createParser(INT16 api_key, INT16 api_version,
                               RequestContextSharedPtr context) const;

  static const RequestParserResolver KAFKA_0_11;

private:
  GeneratorMap generators_;
};

// === HEADER PARSERS ==========================================================

class RequestStartParser : public Parser {
public:
  RequestStartParser(const RequestParserResolver& parser_resolver)
      : parser_resolver_{parser_resolver}, context_{std::make_shared<RequestContext>()} {};

  ParseResponse parse(const char*& buffer, uint64_t& remaining);

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Buffer buffer_;
};

class RequestHeaderBuffer : public CompositeBuffer<RequestHeader, Int16Buffer, Int16Buffer,
                                                   Int32Buffer, NullableStringBuffer> {};

class RequestHeaderParser : public Parser {
public:
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context)
      : parser_resolver_{parser_resolver}, context_{context} {};

  ParseResponse parse(const char*& buffer, uint64_t& remaining);

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderBuffer buffer_;
};

// === BASE REQUEST PARSER =====================================================

template <typename RT, typename BT> class BufferedParser : public Parser {
public:
  BufferedParser(RequestContextSharedPtr ctx) : ctx_{ctx} {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

protected:
  RequestContextSharedPtr ctx_;
  BT buffer_;
};

template <typename RT, typename BT>
ParseResponse BufferedParser<RT, BT>::parse(const char*& buffer, uint64_t& remaining) {
  ctx_->remaining_request_size_ -= buffer_.feed(buffer, remaining);
  if (buffer_.ready()) {
    // after a successful parse, there should be nothing left
    ASSERT(0 == ctx_->remaining_request_size_);
    RT request = buffer_.get();
    request.header() = ctx_->request_header_;
    ENVOY_LOG(trace, "parsed request {}: {}", *ctx_, request);
    MessageSharedPtr msg = std::make_shared<RT>(request);
    return ParseResponse::parsedMessage(msg);
  } else {
    return ParseResponse::stillWaiting();
  }
}

// names of Buffers/Parsers are influenced by org.apache.kafka.common.protocol.Protocol names

// clang-format off
#define DEFINE_REQUEST_PARSER(REQUEST_TYPE, VERSION) \
class REQUEST_TYPE ## VERSION ## Parser :                                      \
  public BufferedParser< REQUEST_TYPE , REQUEST_TYPE ## VERSION ## Buffer >{   \
public:                                                                        \
  REQUEST_TYPE ## VERSION ## Parser(RequestContextSharedPtr ctx):              \
    BufferedParser{ctx} {};                                                    \
};
// clang-format on

// === REQUEST BASE ============================================================

class Request : public Message {
public:
  /**
   * Request header fields need to be initialized by user in case of newly created requests
   */
  Request(INT16 api_key) : request_header_{api_key, 0, 0, ""} {};

  Request(const RequestHeader& request_header) : request_header_{request_header} {};

  RequestHeader& header() { return request_header_; }

  INT16& apiVersion() { return request_header_.api_version_; }

  INT32& correlationId() { return request_header_.correlation_id_; }

  NULLABLE_STRING& clientId() { return request_header_.client_id_; }

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(request_header_.api_key_, dst + written);
    written += encoder.encode(request_header_.api_version_, dst + written);
    written += encoder.encode(request_header_.correlation_id_, dst + written);
    written += encoder.encode(request_header_.client_id_, dst + written);
    written += encodeDetails(dst + written, encoder);
    return written;
  }

  std::ostream& print(std::ostream& os) const override final {
    os << request_header_ << " "; // not very pretty
    return printDetails(os);
  }

protected:
  virtual size_t encodeDetails(char*, Encoder&) const PURE;

  virtual std::ostream& printDetails(std::ostream& os) const PURE;

  RequestHeader request_header_;
};

// === PRODUCE (0) =============================================================

struct ProducePartition {
  INT32 partition_;
  INT32 data_size_;

  friend std::ostream& operator<<(std::ostream& os, const ProducePartition& arg) {
    return os << "{partition = " << arg.partition_ << ", data_size = " << arg.data_size_ << "}";
  }
};

struct ProduceTopic {
  STRING topic_;
  NULLABLE_ARRAY<ProducePartition> partitions_;

  friend std::ostream& operator<<(std::ostream& os, const ProduceTopic& arg) {
    return os << "{topic = " << arg.topic_ << ", partitions = " << arg.partitions_ << "}";
  }
};

class ProduceRequest : public Request {
public:
  // v0 .. v2
  ProduceRequest(INT16 acks, INT32 timeout, NULLABLE_ARRAY<ProduceTopic> topics)
      : ProduceRequest("no-tx-id", acks, timeout, topics){};

  // v3
  ProduceRequest(NULLABLE_STRING transactional_id, INT16 acks, INT32 timeout,
                 NULLABLE_ARRAY<ProduceTopic> topics)
      : Request{RequestType::Produce},
        transactional_id_{transactional_id}, acks_{acks}, timeout_{timeout}, topics_{topics} {};

protected:
  size_t encodeDetails(char*, Encoder&) const override { throw EnvoyException("not implemented"); }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{transactional_id = " << transactional_id_ << ", acks = " << acks_
              << ", timeout = " << timeout_ << ", topics = " << topics_ << "}";
  };

private:
  NULLABLE_STRING transactional_id_;
  INT16 acks_;
  INT32 timeout_;
  NULLABLE_ARRAY<ProduceTopic> topics_;
};

class ProducePartitionBuffer
    : public CompositeBuffer<ProducePartition, Int32Buffer, NullableBytesIgnoringBuffer> {};
class ProducePartitionArrayBuffer : public ArrayBuffer<ProducePartition, ProducePartitionBuffer> {};
class ProduceTopicBuffer
    : public CompositeBuffer<ProduceTopic, StringBuffer, ProducePartitionArrayBuffer> {};
class ProduceTopicArrayBuffer : public ArrayBuffer<ProduceTopic, ProduceTopicBuffer> {};

class ProduceRequestV0Buffer
    : public CompositeBuffer<ProduceRequest, Int16Buffer, Int32Buffer, ProduceTopicArrayBuffer> {};
class ProduceRequestV3Buffer
    : public CompositeBuffer<ProduceRequest, NullableStringBuffer, Int16Buffer, Int32Buffer,
                             ProduceTopicArrayBuffer> {};

DEFINE_REQUEST_PARSER(ProduceRequest, V0);
DEFINE_REQUEST_PARSER(ProduceRequest, V3);

// === FETCH (1) ===============================================================

struct FetchRequestPartition {
  INT32 partition_;
  INT64 fetch_offset_;
  INT64 log_start_offset_;
  INT32 max_bytes_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst + written);
    written += encoder.encode(fetch_offset_, dst + written);
    written += encoder.encode(log_start_offset_, dst + written);
    written += encoder.encode(max_bytes_, dst + written);
    return written;
  }

  friend std::ostream& operator<<(std::ostream& os, const FetchRequestPartition& arg) {
    return os << "{partition = " << arg.partition_ << ", fetch_offset = " << arg.fetch_offset_
              << ", log_start_offset = " << arg.log_start_offset_
              << ", max_bytes = " << arg.max_bytes_ << "}";
  }

  bool operator==(const FetchRequestPartition& rhs) const {
    return partition_ == rhs.partition_ && fetch_offset_ == rhs.fetch_offset_ &&
           log_start_offset_ == rhs.log_start_offset_ && max_bytes_ == rhs.max_bytes_;
  };

  // v0 .. v4
  FetchRequestPartition(INT32 partition, INT64 fetch_offset, INT32 max_bytes)
      : FetchRequestPartition(partition, fetch_offset, -1, max_bytes){};

  // v5
  FetchRequestPartition(INT32 partition, INT64 fetch_offset, INT64 log_start_offset,
                        INT32 max_bytes)
      : partition_{partition}, fetch_offset_{fetch_offset}, log_start_offset_{log_start_offset},
        max_bytes_{max_bytes} {};
};

struct FetchRequestTopic {
  STRING topic_;
  NULLABLE_ARRAY<FetchRequestPartition> partitions_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst + written);
    written += encoder.encode(partitions_, dst + written);
    return written;
  }

  bool operator==(const FetchRequestTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const FetchRequestTopic& arg) {
    return os << "{topic = " << arg.topic_ << ", partitions = " << arg.partitions_ << "}";
  }
};

class FetchRequest : public Request {
public:
  // v0 .. v2
  FetchRequest(INT32 replica_id, INT32 max_wait_time, INT32 min_bytes,
               NULLABLE_ARRAY<FetchRequestTopic> topics)
      : FetchRequest(replica_id, max_wait_time, min_bytes, -1, topics){};

  // v3
  FetchRequest(INT32 replica_id, INT32 max_wait_time, INT32 min_bytes, INT32 max_bytes,
               NULLABLE_ARRAY<FetchRequestTopic> topics)
      : FetchRequest(replica_id, max_wait_time, min_bytes, max_bytes, -1, topics){};

  // v4 .. v5
  FetchRequest(INT32 replica_id, INT32 max_wait_time, INT32 min_bytes, INT32 max_bytes,
               INT8 isolation_level, NULLABLE_ARRAY<FetchRequestTopic> topics)
      : Request{RequestType::Fetch}, replica_id_{replica_id}, max_wait_time_{max_wait_time},
        min_bytes_{min_bytes}, max_bytes_{max_bytes},
        isolation_level_{isolation_level}, topics_{topics} {};

  bool operator==(const FetchRequest& rhs) const {
    return request_header_ == rhs.request_header_ && replica_id_ == rhs.replica_id_ &&
           max_wait_time_ == rhs.max_wait_time_ && min_bytes_ == rhs.min_bytes_ &&
           max_bytes_ == rhs.max_bytes_ && isolation_level_ == rhs.isolation_level_ &&
           topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(char* dst, Encoder& encoder) const override {
    size_t written{0};
    written += encoder.encode(replica_id_, dst + written);
    written += encoder.encode(max_wait_time_, dst + written);
    written += encoder.encode(min_bytes_, dst + written);
    written += encoder.encode(max_bytes_, dst + written);
    written += encoder.encode(isolation_level_, dst + written);
    written += encoder.encode(topics_, dst + written);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{replica_id = " << replica_id_ << ", max_wait_time = " << max_wait_time_
              << ", min_bytes = " << min_bytes_ << ", max_bytes = " << max_bytes_
              << ", isolation_level = " << static_cast<uint32_t>(isolation_level_)
              << ", topics = " << topics_ << "}";
  }

private:
  INT32 replica_id_;
  INT32 max_wait_time_;
  INT32 min_bytes_;
  INT32 max_bytes_;
  INT8 isolation_level_;
  NULLABLE_ARRAY<FetchRequestTopic> topics_;
};

class FetchRequestPartitionV0Buffer
    : public CompositeBuffer<FetchRequestPartition, Int32Buffer, Int64Buffer, Int32Buffer> {};
class FetchRequestPartitionV0ArrayBuffer
    : public ArrayBuffer<FetchRequestPartition, FetchRequestPartitionV0Buffer> {};
class FetchRequestTopicV0Buffer
    : public CompositeBuffer<FetchRequestTopic, StringBuffer, FetchRequestPartitionV0ArrayBuffer> {
};
class FetchRequestTopicV0ArrayBuffer
    : public ArrayBuffer<FetchRequestTopic, FetchRequestTopicV0Buffer> {};

class FetchRequestPartitionV5Buffer
    : public CompositeBuffer<FetchRequestPartition, Int32Buffer, Int64Buffer, Int64Buffer,
                             Int32Buffer> {};
class FetchRequestPartitionV5ArrayBuffer
    : public ArrayBuffer<FetchRequestPartition, FetchRequestPartitionV5Buffer> {};
class FetchRequestTopicV5Buffer
    : public CompositeBuffer<FetchRequestTopic, StringBuffer, FetchRequestPartitionV5ArrayBuffer> {
};
class FetchRequestTopicV5ArrayBuffer
    : public ArrayBuffer<FetchRequestTopic, FetchRequestTopicV5Buffer> {};

class FetchRequestV0Buffer : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer,
                                                    Int32Buffer, FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV3Buffer
    : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer,
                             FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV4Buffer
    : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer,
                             Int8Buffer, FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV5Buffer
    : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer,
                             Int8Buffer, FetchRequestTopicV5ArrayBuffer> {};

DEFINE_REQUEST_PARSER(FetchRequest, V0);
DEFINE_REQUEST_PARSER(FetchRequest, V3);
DEFINE_REQUEST_PARSER(FetchRequest, V4);
DEFINE_REQUEST_PARSER(FetchRequest, V5);

// === LIST OFFSETS (2) ========================================================

struct ListOffsetsPartition {
  INT32 partition_;
  INT64 timestamp_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst + written);
    written += encoder.encode(timestamp_, dst + written);
    return written;
  }

  bool operator==(const ListOffsetsPartition& rhs) const {
    return partition_ == rhs.partition_ && timestamp_ == rhs.timestamp_;
  };

  friend std::ostream& operator<<(std::ostream& os, const ListOffsetsPartition& arg) {
    return os << "{partition = " << arg.partition_ << ", timestamp = " << arg.timestamp_ << "}";
  }

  // v0
  ListOffsetsPartition(INT32 partition, INT64 timestamp, INT32 /* (ignored) max_num_offsets */)
      : ListOffsetsPartition(partition, timestamp){};

  // v1 .. v2
  ListOffsetsPartition(INT32 partition, INT64 timestamp)
      : partition_{partition}, timestamp_{timestamp} {};
};

struct ListOffsetsTopic {
  STRING topic_;
  NULLABLE_ARRAY<ListOffsetsPartition> partitions_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst + written);
    written += encoder.encode(partitions_, dst + written);
    return written;
  }

  bool operator==(const ListOffsetsTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const ListOffsetsTopic& arg) {
    return os << "{topic = " << arg.topic_ << ", partitions = " << arg.partitions_ << "}";
  }
};

class ListOffsetsRequest : public Request {
public:
  // v0 .. v1
  ListOffsetsRequest(INT32 replica_id, NULLABLE_ARRAY<ListOffsetsTopic> topics)
      : ListOffsetsRequest(replica_id, -1, topics){};

  // v2
  ListOffsetsRequest(INT32 replica_id, INT8 isolation_level,
                     NULLABLE_ARRAY<ListOffsetsTopic> topics)
      : Request{RequestType::ListOffsets}, replica_id_{replica_id},
        isolation_level_{isolation_level}, topics_{topics} {};

  bool operator==(const ListOffsetsRequest& rhs) const {
    return request_header_ == rhs.request_header_ && replica_id_ == rhs.replica_id_ &&
           isolation_level_ == rhs.isolation_level_ && topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(char* dst, Encoder& encoder) const override {
    size_t written{0};
    written += encoder.encode(replica_id_, dst + written);
    written += encoder.encode(isolation_level_, dst + written);
    written += encoder.encode(topics_, dst + written);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{replica_id = " << replica_id_
              << ", isolation_level = " << static_cast<uint32_t>(isolation_level_)
              << ", topics = " << topics_ << "}";
  }

private:
  INT32 replica_id_;
  INT8 isolation_level_;
  NULLABLE_ARRAY<ListOffsetsTopic> topics_;
};

class ListOffsetsPartitionV0Buffer
    : public CompositeBuffer<ListOffsetsPartition, Int32Buffer, Int64Buffer, Int32Buffer> {};
class ListOffsetsPartitionV0ArrayBuffer
    : public ArrayBuffer<ListOffsetsPartition, ListOffsetsPartitionV0Buffer> {};
class ListOffsetsTopicV0Buffer
    : public CompositeBuffer<ListOffsetsTopic, StringBuffer, ListOffsetsPartitionV0ArrayBuffer> {};
class ListOffsetsTopicV0ArrayBuffer
    : public ArrayBuffer<ListOffsetsTopic, ListOffsetsTopicV0Buffer> {};

class ListOffsetsPartitionV1Buffer
    : public CompositeBuffer<ListOffsetsPartition, Int32Buffer, Int64Buffer> {};
class ListOffsetsPartitionV1ArrayBuffer
    : public ArrayBuffer<ListOffsetsPartition, ListOffsetsPartitionV1Buffer> {};
class ListOffsetsTopicV1Buffer
    : public CompositeBuffer<ListOffsetsTopic, StringBuffer, ListOffsetsPartitionV1ArrayBuffer> {};
class ListOffsetsTopicV1ArrayBuffer
    : public ArrayBuffer<ListOffsetsTopic, ListOffsetsTopicV1Buffer> {};

class ListOffsetsRequestV0Buffer
    : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, ListOffsetsTopicV0ArrayBuffer> {};
class ListOffsetsRequestV1Buffer
    : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, ListOffsetsTopicV1ArrayBuffer> {};
class ListOffsetsRequestV2Buffer
    : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, Int8Buffer,
                             ListOffsetsTopicV1ArrayBuffer> {};

DEFINE_REQUEST_PARSER(ListOffsetsRequest, V0);
DEFINE_REQUEST_PARSER(ListOffsetsRequest, V1);
DEFINE_REQUEST_PARSER(ListOffsetsRequest, V2);

// === METADATA (3) ============================================================

class MetadataRequest : public Request {
public:
  // v0 .. v3
  MetadataRequest(NULLABLE_ARRAY<STRING> topics) : MetadataRequest(topics, false){};

  // v4
  MetadataRequest(NULLABLE_ARRAY<STRING> topics, BOOLEAN allow_auto_topic_creation)
      : Request{RequestType::Metadata}, topics_{topics}, allow_auto_topic_creation_{
                                                             allow_auto_topic_creation} {};

  bool operator==(const MetadataRequest& rhs) const {
    return request_header_ == rhs.request_header_ && topics_ == rhs.topics_ &&
           allow_auto_topic_creation_ == rhs.allow_auto_topic_creation_;
  };

protected:
  size_t encodeDetails(char* dst, Encoder& encoder) const override {
    size_t written{0};
    written += encoder.encode(topics_, dst + written);
    written += encoder.encode(allow_auto_topic_creation_, dst + written);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{topics = " << topics_
              << ", allow_auto_topic_creation = " << allow_auto_topic_creation_ << "}";
  }

private:
  NULLABLE_ARRAY<STRING> topics_;
  BOOLEAN allow_auto_topic_creation_;
};

class MetadataRequestTopicV0Buffer : public ArrayBuffer<STRING, StringBuffer> {};
class MetadataRequestV0Buffer
    : public CompositeBuffer<MetadataRequest, MetadataRequestTopicV0Buffer> {};
class MetadataRequestV4Buffer
    : public CompositeBuffer<MetadataRequest, MetadataRequestTopicV0Buffer, BoolBuffer> {};

DEFINE_REQUEST_PARSER(MetadataRequest, V0);
DEFINE_REQUEST_PARSER(MetadataRequest, V4);

// === OFFSET COMMIT (8) =======================================================

struct OffsetCommitPartition {
  INT32 partition_;
  INT64 offset_;
  NULLABLE_STRING metadata_;

  // v0 *and* v2
  OffsetCommitPartition(INT32 partition, INT64 offset, NULLABLE_STRING metadata)
      : partition_{partition}, offset_{offset}, metadata_{metadata} {};

  // v1
  OffsetCommitPartition(INT32 partition, INT64 offset, INT64 /* timestamp */,
                        NULLABLE_STRING metadata)
      : partition_{partition}, offset_{offset}, metadata_{metadata} {};

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst + written);
    written += encoder.encode(offset_, dst + written);
    written += encoder.encode(metadata_, dst + written);
    return written;
  }

  bool operator==(const OffsetCommitPartition& rhs) const {
    return partition_ == rhs.partition_ && offset_ == rhs.offset_ && metadata_ == rhs.metadata_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetCommitPartition& arg) {
    return os << "{partition = " << arg.partition_ << ", offset = " << arg.offset_
              << ", metadata = " << arg.metadata_ << "}";
  }
};

struct OffsetCommitTopic {
  STRING topic_;
  NULLABLE_ARRAY<OffsetCommitPartition> partitions_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst + written);
    written += encoder.encode(partitions_, dst + written);
    return written;
  }

  bool operator==(const OffsetCommitTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetCommitTopic& arg) {
    return os << "{topic = " << arg.topic_ << ", partitions_ = " << arg.partitions_ << "}";
  }
};

class OffsetCommitRequest : public Request {
public:
  // v0
  OffsetCommitRequest(STRING group_id, NULLABLE_ARRAY<OffsetCommitTopic> topics)
      : OffsetCommitRequest(group_id, -1, "no-member-id", -1, topics){};

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
  size_t encodeDetails(char* dst, Encoder& encoder) const override {
    size_t written{0};
    written += encoder.encode(group_id_, dst + written);
    written += encoder.encode(group_generation_id_, dst + written);
    written += encoder.encode(member_id_, dst + written);
    written += encoder.encode(retention_time_, dst + written);
    written += encoder.encode(topics_, dst + written);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{group_id = " << group_id_ << ", group_generation_id = " << group_generation_id_
              << ", member_id = " << member_id_ << ", retention_time = " << retention_time_
              << ", topics = " << topics_ << "}";
  }

private:
  STRING group_id_;
  INT32 group_generation_id_;
  STRING member_id_;
  INT64 retention_time_;
  NULLABLE_ARRAY<OffsetCommitTopic> topics_;
};

class OffsetCommitPartitionV0Buffer : public CompositeBuffer<OffsetCommitPartition, Int32Buffer,
                                                             Int64Buffer, NullableStringBuffer> {};
class OffsetCommitPartitionV0ArrayBuffer
    : public ArrayBuffer<OffsetCommitPartition, OffsetCommitPartitionV0Buffer> {};
class OffsetCommitTopicV0Buffer
    : public CompositeBuffer<OffsetCommitTopic, StringBuffer, OffsetCommitPartitionV0ArrayBuffer> {
};
class OffsetCommitTopicV0ArrayBuffer
    : public ArrayBuffer<OffsetCommitTopic, OffsetCommitTopicV0Buffer> {};

class OffsetCommitPartitionV1Buffer
    : public CompositeBuffer<OffsetCommitPartition, Int32Buffer, Int64Buffer, Int64Buffer,
                             NullableStringBuffer> {};
class OffsetCommitPartitionV1ArrayBuffer
    : public ArrayBuffer<OffsetCommitPartition, OffsetCommitPartitionV1Buffer> {};
class OffsetCommitTopicV1Buffer
    : public CompositeBuffer<OffsetCommitTopic, StringBuffer, OffsetCommitPartitionV1ArrayBuffer> {
};
class OffsetCommitTopicV1ArrayBuffer
    : public ArrayBuffer<OffsetCommitTopic, OffsetCommitTopicV1Buffer> {};

class OffsetCommitRequestV0Buffer
    : public CompositeBuffer<OffsetCommitRequest, StringBuffer, OffsetCommitTopicV0ArrayBuffer> {};
class OffsetCommitRequestV1Buffer
    : public CompositeBuffer<OffsetCommitRequest, StringBuffer, Int32Buffer, StringBuffer,
                             OffsetCommitTopicV1ArrayBuffer> {};
class OffsetCommitRequestV2Buffer
    : public CompositeBuffer<OffsetCommitRequest, StringBuffer, Int32Buffer, StringBuffer,
                             Int64Buffer, OffsetCommitTopicV0ArrayBuffer> {};

DEFINE_REQUEST_PARSER(OffsetCommitRequest, V0);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V1);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V2);

// === OFFSET FETCH (9) ========================================================

struct OffsetFetchTopic {
  STRING topic_;
  NULLABLE_ARRAY<INT32> partitions_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst + written);
    written += encoder.encode(partitions_, dst + written);
    return written;
  }

  bool operator==(const OffsetFetchTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetFetchTopic& arg) {
    return os << "{topic = " << arg.topic_ << ", partitions = " << arg.partitions_ << "}";
  }
};

class OffsetFetchRequest : public Request {
public:
  // v0 .. v2
  OffsetFetchRequest(STRING group_id, NULLABLE_ARRAY<OffsetFetchTopic> topics)
      : Request{RequestType::OffsetFetch}, group_id_{group_id}, topics_{topics} {};

  bool operator==(const OffsetFetchRequest& rhs) const {
    return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ &&
           topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(char* dst, Encoder& encoder) const override {
    size_t written{0};
    written += encoder.encode(group_id_, dst + written);
    written += encoder.encode(topics_, dst + written);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{group_id = " << group_id_ << ", topics = " << topics_ << "}";
  }

private:
  STRING group_id_;
  NULLABLE_ARRAY<OffsetFetchTopic> topics_;
};

class OffsetFetchPartitionV0ArrayBuffer : public ArrayBuffer<INT32, Int32Buffer> {};
class OffsetFetchTopicV0Buffer
    : public CompositeBuffer<OffsetFetchTopic, StringBuffer, OffsetFetchPartitionV0ArrayBuffer> {};
class OffsetFetchTopicV0ArrayBuffer
    : public ArrayBuffer<OffsetFetchTopic, OffsetFetchTopicV0Buffer> {};

class OffsetFetchRequestV0Buffer
    : public CompositeBuffer<OffsetFetchRequest, StringBuffer, OffsetFetchTopicV0ArrayBuffer> {};
class OffsetFetchRequestV2Buffer
    : public CompositeBuffer<OffsetFetchRequest, StringBuffer, OffsetFetchTopicV0ArrayBuffer> {};

DEFINE_REQUEST_PARSER(OffsetFetchRequest, V0);
DEFINE_REQUEST_PARSER(OffsetFetchRequest, V2);

// === API VERSIONS (18) =======================================================

class ApiVersionsRequest : public Request {
public:
  // v0 .. v1
  ApiVersionsRequest() : Request{RequestType::ApiVersions} {};

  bool operator==(const ApiVersionsRequest& rhs) const {
    return request_header_ == rhs.request_header_;
  };

protected:
  size_t encodeDetails(char*, Encoder&) const override { return 0; }

  std::ostream& printDetails(std::ostream& os) const override { return os << "{}"; }
};

class ApiVersionsRequestV0Buffer : public NullBuffer<ApiVersionsRequest> {};

DEFINE_REQUEST_PARSER(ApiVersionsRequest, V0);

// === UNKNOWN REQUEST =========================================================

class UnknownRequest : public Request {
public:
  UnknownRequest(const RequestHeader& request_header) : Request{request_header} {};

protected:
  // this isn't the prettiest, as we have thrown away the data
  // XXX(adam.kotwasinski) discuss capturing the data as-is, and simply putting it back
  //   this would add ability to forward unknown types of requests
  size_t encodeDetails(char*, Encoder&) const override {
    throw EnvoyException("cannot serialize unknown request");
  }

  std::ostream& printDetails(std::ostream& out) const override {
    return out << "{unknown request}";
  }
};

// ignores data until the end of request (contained in c)
class SentinelConsumer : public Parser {
public:
  SentinelConsumer(RequestContextSharedPtr context) : context_{context} {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

private:
  const RequestContextSharedPtr context_;
};

// === REQUEST SERIALIZER ======================================================

class RequestSerializer {
public:
  size_t encode(const Request& request, char* dst);
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
