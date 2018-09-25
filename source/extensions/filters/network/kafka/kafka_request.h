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

// === PRODUCE (0) =============================================================

/**
 * Produce request parser is a special case that has two corresponding parsers
 * One parser captures data, the other one does not, only saving the length of data provided
 * This might be used in filters that do not need access to data (e.g. only want to update request
 * type metrics)
 */

// holds data sent by client
struct FatProducePartition {
  const INT32 partition_;
  const NULLABLE_BYTES data_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst);
    written += encoder.encode(data_, dst);
    return written;
  }

  bool operator==(const FatProducePartition& rhs) const {
    return partition_ == rhs.partition_ && data_ == rhs.data_;
  };

  friend std::ostream& operator<<(std::ostream& os, const FatProducePartition& arg) {
    os << "{partition=" << arg.partition_ << ", data(size)=";
    if (arg.data_.has_value()) {
      os << arg.data_->size();
    } else {
      os << "<null>";
    }
    return os << "}";
  }
};

// does not carry data, only its length
struct ThinProducePartition {
  const INT32 partition_;
  const INT32 data_size_;

  size_t encode(Buffer::Instance&, EncodingContext&) const {
    throw EnvoyException("ThinProducePartition cannot be encoded");
  }

  bool operator==(const ThinProducePartition& rhs) const {
    return partition_ == rhs.partition_ && data_size_ == rhs.data_size_;
  };

  friend std::ostream& operator<<(std::ostream& os, const ThinProducePartition& arg) {
    return os << "{partition=" << arg.partition_ << ", data_size=" << arg.data_size_ << "}";
  }
};

template <typename PT> struct ProduceTopic {
  const STRING topic_;
  const NULLABLE_ARRAY<PT> partitions_;

  bool operator==(const ProduceTopic<PT>& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  friend std::ostream& operator<<(std::ostream& os, const ProduceTopic& arg) {
    return os << "{topic=" << arg.topic_ << ", partitions=" << arg.partitions_ << "}";
  }
};

typedef ProduceTopic<FatProducePartition> FatProduceTopic;
typedef ProduceTopic<ThinProducePartition> ThinProduceTopic;

/**
 * PT carries partition type, which can be capturing (contains bytes) or non-capturing (contains
 * bytes' length only)
 */
template <typename PT> class ProduceRequest : public Request {
public:
  // v0 .. v2
  ProduceRequest(INT16 acks, INT32 timeout, NULLABLE_ARRAY<ProduceTopic<PT>> topics)
      : ProduceRequest(absl::nullopt, acks, timeout, topics){};

  // v3
  ProduceRequest(NULLABLE_STRING transactional_id, INT16 acks, INT32 timeout,
                 NULLABLE_ARRAY<ProduceTopic<PT>> topics)
      : Request{RequestType::Produce},
        transactional_id_{transactional_id}, acks_{acks}, timeout_{timeout}, topics_{topics} {};

  bool operator==(const ProduceRequest<PT>& rhs) const {
    return request_header_ == rhs.request_header_ && transactional_id_ == rhs.transactional_id_ &&
           acks_ == rhs.acks_ && timeout_ == rhs.timeout_ && topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    if (request_header_.api_version_ >= 3) {
      written += encoder.encode(transactional_id_, dst);
    }
    written += encoder.encode(acks_, dst);
    written += encoder.encode(timeout_, dst);
    written += encoder.encode(topics_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{transactional_id=" << transactional_id_ << ", acks=" << acks_
              << ", timeout=" << timeout_ << ", topics=" << topics_ << "}";
  };

private:
  const NULLABLE_STRING transactional_id_;
  const INT16 acks_;
  const INT32 timeout_;
  const NULLABLE_ARRAY<ProduceTopic<PT>> topics_;
};

typedef ProduceRequest<FatProducePartition> FatProduceRequest;
typedef ProduceRequest<ThinProducePartition> ThinProduceRequest;

// clang-format off
class ThinProducePartitionArrayBuffer : public ArrayBuffer<ThinProducePartition, CompositeBuffer<ThinProducePartition, Int32Buffer, NullableBytesIgnoringBuffer>> {};
class ProducePartitionArrayBuffer : public ArrayBuffer<FatProducePartition, CompositeBuffer<FatProducePartition, Int32Buffer, NullableBytesCapturingBuffer>> {};

class ThinProduceTopicArrayBuffer : public ArrayBuffer<ThinProduceTopic, CompositeBuffer<ThinProduceTopic, StringBuffer, ThinProducePartitionArrayBuffer>> {};
class ProduceTopicArrayBuffer : public ArrayBuffer<FatProduceTopic, CompositeBuffer<FatProduceTopic, StringBuffer, ProducePartitionArrayBuffer>> {};

class ThinProduceRequestV0Buffer : public CompositeBuffer<ThinProduceRequest, Int16Buffer, Int32Buffer, ThinProduceTopicArrayBuffer> {};
class ThinProduceRequestV3Buffer : public CompositeBuffer<ThinProduceRequest, NullableStringBuffer, Int16Buffer, Int32Buffer, ThinProduceTopicArrayBuffer> {};
class FatProduceRequestV0Buffer : public CompositeBuffer<FatProduceRequest, Int16Buffer, Int32Buffer, ProduceTopicArrayBuffer> {};
class FatProduceRequestV3Buffer : public CompositeBuffer<FatProduceRequest, NullableStringBuffer, Int16Buffer, Int32Buffer, ProduceTopicArrayBuffer> {};

DEFINE_REQUEST_PARSER(ThinProduceRequest, V0);
DEFINE_REQUEST_PARSER(ThinProduceRequest, V3);
DEFINE_REQUEST_PARSER(FatProduceRequest, V0);
DEFINE_REQUEST_PARSER(FatProduceRequest, V3);
// clang-format on

// === FETCH (1) ===============================================================

struct FetchRequestPartition {
  const INT32 partition_;
  const INT64 fetch_offset_;
  const INT64 log_start_offset_; // since v5
  const INT32 max_bytes_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst);
    written += encoder.encode(fetch_offset_, dst);
    if (encoder.apiVersion() >= 5) {
      written += encoder.encode(log_start_offset_, dst);
    }
    written += encoder.encode(max_bytes_, dst);
    return written;
  }

  friend std::ostream& operator<<(std::ostream& os, const FetchRequestPartition& arg) {
    return os << "{partition=" << arg.partition_ << ", fetch_offset=" << arg.fetch_offset_
              << ", log_start_offset=" << arg.log_start_offset_ << ", max_bytes=" << arg.max_bytes_
              << "}";
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
  const STRING topic_;
  const NULLABLE_ARRAY<FetchRequestPartition> partitions_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  bool operator==(const FetchRequestTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const FetchRequestTopic& arg) {
    return os << "{topic=" << arg.topic_ << ", partitions=" << arg.partitions_ << "}";
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
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    INT16 api_version = request_header_.api_version_;
    written += encoder.encode(replica_id_, dst);
    written += encoder.encode(max_wait_time_, dst);
    written += encoder.encode(min_bytes_, dst);
    if (api_version >= 3) {
      written += encoder.encode(max_bytes_, dst);
    }
    if (api_version >= 4) {
      written += encoder.encode(isolation_level_, dst);
    }
    written += encoder.encode(topics_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{replica_id=" << replica_id_ << ", max_wait_time=" << max_wait_time_
              << ", min_bytes=" << min_bytes_ << ", max_bytes=" << max_bytes_
              << ", isolation_level=" << static_cast<uint32_t>(isolation_level_)
              << ", topics=" << topics_ << "}";
  }

private:
  const INT32 replica_id_;
  const INT32 max_wait_time_;
  const INT32 min_bytes_;
  const INT32 max_bytes_;      // since v3
  const INT8 isolation_level_; // since v4
  const NULLABLE_ARRAY<FetchRequestTopic> topics_;
};

// clang-format off
class FetchRequestPartitionV0Buffer : public CompositeBuffer<FetchRequestPartition, Int32Buffer, Int64Buffer, Int32Buffer> {};
class FetchRequestPartitionV0ArrayBuffer : public ArrayBuffer<FetchRequestPartition, FetchRequestPartitionV0Buffer> {};
class FetchRequestTopicV0Buffer : public CompositeBuffer<FetchRequestTopic, StringBuffer, FetchRequestPartitionV0ArrayBuffer> {};
class FetchRequestTopicV0ArrayBuffer : public ArrayBuffer<FetchRequestTopic, FetchRequestTopicV0Buffer> {};

class FetchRequestPartitionV5Buffer : public CompositeBuffer<FetchRequestPartition, Int32Buffer, Int64Buffer, Int64Buffer, Int32Buffer> {};
class FetchRequestPartitionV5ArrayBuffer : public ArrayBuffer<FetchRequestPartition, FetchRequestPartitionV5Buffer> {};
class FetchRequestTopicV5Buffer : public CompositeBuffer<FetchRequestTopic, StringBuffer, FetchRequestPartitionV5ArrayBuffer> {};
class FetchRequestTopicV5ArrayBuffer : public ArrayBuffer<FetchRequestTopic, FetchRequestTopicV5Buffer> {};

class FetchRequestV0Buffer : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV3Buffer : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer, FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV4Buffer : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer, Int8Buffer, FetchRequestTopicV0ArrayBuffer> {};
class FetchRequestV5Buffer : public CompositeBuffer<FetchRequest, Int32Buffer, Int32Buffer, Int32Buffer, Int32Buffer, Int8Buffer, FetchRequestTopicV5ArrayBuffer> {};

DEFINE_REQUEST_PARSER(FetchRequest, V0);
DEFINE_REQUEST_PARSER(FetchRequest, V3);
DEFINE_REQUEST_PARSER(FetchRequest, V4);
DEFINE_REQUEST_PARSER(FetchRequest, V5);
// clang-format on

// === LIST OFFSETS (2) ========================================================

struct ListOffsetsPartition {
  const INT32 partition_;
  const INT64 timestamp_;
  const INT32 max_num_offsets_; // only v0

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(partition_, dst);
    written += encoder.encode(timestamp_, dst);
    if (encoder.apiVersion() == 0) {
      written += encoder.encode(max_num_offsets_, dst);
    }
    return written;
  }

  bool operator==(const ListOffsetsPartition& rhs) const {
    return partition_ == rhs.partition_ && timestamp_ == rhs.timestamp_ &&
           max_num_offsets_ == rhs.max_num_offsets_;
  };

  friend std::ostream& operator<<(std::ostream& os, const ListOffsetsPartition& arg) {
    return os << "{partition=" << arg.partition_ << ", timestamp=" << arg.timestamp_
              << ", max_num_offsets=" << arg.max_num_offsets_ << "}";
  }

  // v0
  ListOffsetsPartition(INT32 partition, INT64 timestamp, INT32 max_num_offsets)
      : partition_{partition}, timestamp_{timestamp}, max_num_offsets_{max_num_offsets} {};

  // v1 .. v2
  ListOffsetsPartition(INT32 partition, INT64 timestamp)
      : ListOffsetsPartition(partition, timestamp, -1){};
};

struct ListOffsetsTopic {
  const STRING topic_;
  const NULLABLE_ARRAY<ListOffsetsPartition> partitions_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  bool operator==(const ListOffsetsTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const ListOffsetsTopic& arg) {
    return os << "{topic=" << arg.topic_ << ", partitions=" << arg.partitions_ << "}";
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
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(replica_id_, dst);
    if (encoder.apiVersion() >= 2) {
      written += encoder.encode(isolation_level_, dst);
    }
    written += encoder.encode(topics_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{replica_id=" << replica_id_
              << ", isolation_level=" << static_cast<uint32_t>(isolation_level_)
              << ", topics=" << topics_ << "}";
  }

private:
  const INT32 replica_id_;
  const INT8 isolation_level_; // since v2
  const NULLABLE_ARRAY<ListOffsetsTopic> topics_;
};

// clang-format off
class ListOffsetsPartitionV0Buffer : public CompositeBuffer<ListOffsetsPartition, Int32Buffer, Int64Buffer, Int32Buffer> {};
class ListOffsetsPartitionV0ArrayBuffer : public ArrayBuffer<ListOffsetsPartition, ListOffsetsPartitionV0Buffer> {};
class ListOffsetsTopicV0Buffer : public CompositeBuffer<ListOffsetsTopic, StringBuffer, ListOffsetsPartitionV0ArrayBuffer> {};
class ListOffsetsTopicV0ArrayBuffer : public ArrayBuffer<ListOffsetsTopic, ListOffsetsTopicV0Buffer> {};

class ListOffsetsPartitionV1Buffer : public CompositeBuffer<ListOffsetsPartition, Int32Buffer, Int64Buffer> {};
class ListOffsetsPartitionV1ArrayBuffer : public ArrayBuffer<ListOffsetsPartition, ListOffsetsPartitionV1Buffer> {};
class ListOffsetsTopicV1Buffer : public CompositeBuffer<ListOffsetsTopic, StringBuffer, ListOffsetsPartitionV1ArrayBuffer> {};
class ListOffsetsTopicV1ArrayBuffer : public ArrayBuffer<ListOffsetsTopic, ListOffsetsTopicV1Buffer> {};

class ListOffsetsRequestV0Buffer : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, ListOffsetsTopicV0ArrayBuffer> {};
class ListOffsetsRequestV1Buffer : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, ListOffsetsTopicV1ArrayBuffer> {};
class ListOffsetsRequestV2Buffer : public CompositeBuffer<ListOffsetsRequest, Int32Buffer, Int8Buffer, ListOffsetsTopicV1ArrayBuffer> {};

DEFINE_REQUEST_PARSER(ListOffsetsRequest, V0);
DEFINE_REQUEST_PARSER(ListOffsetsRequest, V1);
DEFINE_REQUEST_PARSER(ListOffsetsRequest, V2);
// clang-format on

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
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(topics_, dst);
    if (encoder.apiVersion() >= 2) {
      written += encoder.encode(allow_auto_topic_creation_, dst);
    }
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{topics=" << topics_
              << ", allow_auto_topic_creation=" << allow_auto_topic_creation_ << "}";
  }

private:
  NULLABLE_ARRAY<STRING> topics_;
  BOOLEAN allow_auto_topic_creation_; // since v4
};

// clang-format off
class MetadataRequestTopicV0Buffer : public ArrayBuffer<STRING, StringBuffer> {};
class MetadataRequestV0Buffer : public CompositeBuffer<MetadataRequest, MetadataRequestTopicV0Buffer> {};
class MetadataRequestV4Buffer : public CompositeBuffer<MetadataRequest, MetadataRequestTopicV0Buffer, BoolBuffer> {};

DEFINE_REQUEST_PARSER(MetadataRequest, V0);
DEFINE_REQUEST_PARSER(MetadataRequest, V4);
// clang-format on

// === LEADER-AND-ISR (4) ======================================================

/**
 * This structure is used in both LeaderAndIsr v0 & UpdateMetadata
 */

struct MetadataPartitionState {
  const STRING topic_;
  const INT32 partition_;
  const INT32 controller_epoch_;
  const INT32 leader_;
  const INT32 leader_epoch_;
  const NULLABLE_ARRAY<INT32> isr_;
  const INT32 zk_version_;
  const NULLABLE_ARRAY<INT32> replicas_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partition_, dst);
    written += encoder.encode(controller_epoch_, dst);
    written += encoder.encode(leader_, dst);
    written += encoder.encode(leader_epoch_, dst);
    written += encoder.encode(isr_, dst);
    written += encoder.encode(zk_version_, dst);
    written += encoder.encode(replicas_, dst);
    return written;
  }

  bool operator==(const MetadataPartitionState& rhs) const {
    return topic_ == rhs.topic_ && partition_ == rhs.partition_ &&
           controller_epoch_ == rhs.controller_epoch_ && leader_ == rhs.leader_ &&
           leader_epoch_ == rhs.leader_epoch_ && isr_ == rhs.isr_ &&
           zk_version_ == rhs.zk_version_ && replicas_ == rhs.replicas_;
  };

  friend std::ostream& operator<<(std::ostream& os, const MetadataPartitionState& arg) {
    return os << "{topic=" << arg.topic_ << ", partition=" << arg.partition_
              << ", controller_epoch=" << arg.controller_epoch_ << ", leader=" << arg.leader_
              << ", leader_epoch=" << arg.leader_epoch_ << ", isr=" << arg.isr_
              << ", zk_version=" << arg.zk_version_ << ", zk_version=" << arg.zk_version_ << "}";
  }
};

struct LeaderAndIsrLiveLeader {
  const INT32 id_;
  const STRING host_;
  const INT32 port_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(id_, dst);
    written += encoder.encode(host_, dst);
    written += encoder.encode(port_, dst);
    return written;
  }

  bool operator==(const LeaderAndIsrLiveLeader& rhs) const {
    return id_ == rhs.id_ && host_ == rhs.host_ && port_ == rhs.port_;
  };

  friend std::ostream& operator<<(std::ostream& os, const LeaderAndIsrLiveLeader& arg) {
    return os << "{id=" << arg.id_ << ", host=" << arg.host_ << ", port=" << arg.port_ << "}";
  }
};

class LeaderAndIsrRequest : public Request {
public:
  // v0
  LeaderAndIsrRequest(INT32 controller_id, INT32 controller_epoch,
                      NULLABLE_ARRAY<MetadataPartitionState> partition_states,
                      NULLABLE_ARRAY<LeaderAndIsrLiveLeader> live_readers)
      : Request{RequestType::LeaderAndIsr}, controller_id_{controller_id},
        controller_epoch_{controller_epoch}, partition_states_{partition_states},
        live_readers_{live_readers} {};

  bool operator==(const LeaderAndIsrRequest& rhs) const {
    return request_header_ == rhs.request_header_ && controller_id_ == rhs.controller_id_ &&
           controller_epoch_ == rhs.controller_epoch_ &&
           partition_states_ == rhs.partition_states_ && live_readers_ == rhs.live_readers_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(controller_id_, dst);
    written += encoder.encode(controller_epoch_, dst);
    written += encoder.encode(partition_states_, dst);
    written += encoder.encode(live_readers_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{controller_id=" << controller_id_ << ", controller_epoch=" << controller_epoch_
              << ", partition_states=" << partition_states_ << ", live_readers=" << live_readers_
              << "}";
  }

private:
  const INT32 controller_id_;
  const INT32 controller_epoch_;
  const NULLABLE_ARRAY<MetadataPartitionState> partition_states_;
  const NULLABLE_ARRAY<LeaderAndIsrLiveLeader> live_readers_;
};

// clang-format off
class MetadataPartitionStateV0Buffer : public CompositeBuffer<MetadataPartitionState,
                                                              StringBuffer,
                                                              Int32Buffer,
                                                              Int32Buffer,
                                                              Int32Buffer,
                                                              Int32Buffer,
                                                              ArrayBuffer<INT32, Int32Buffer>,
                                                              Int32Buffer,
                                                              ArrayBuffer<INT32, Int32Buffer>
                                                              > {};
class MetadataPartitionStateV0ArrayBuffer : public ArrayBuffer<MetadataPartitionState, MetadataPartitionStateV0Buffer> {};

class LeaderAndIsrLiveLeaderV0Buffer : public CompositeBuffer<LeaderAndIsrLiveLeader, Int32Buffer, StringBuffer, Int32Buffer> {};
class LeaderAndIsrLiveLeaderV0ArrayBuffer : public ArrayBuffer<LeaderAndIsrLiveLeader, LeaderAndIsrLiveLeaderV0Buffer> {};

class LeaderAndIsrRequestV0Buffer : public CompositeBuffer<LeaderAndIsrRequest, Int32Buffer, Int32Buffer, MetadataPartitionStateV0ArrayBuffer, LeaderAndIsrLiveLeaderV0ArrayBuffer> {};

DEFINE_REQUEST_PARSER(LeaderAndIsrRequest, V0);
// clang-format on

// === STOP REPLICA (5) ========================================================

struct StopReplicaPartition {
  const STRING topic_;
  const INT32 partition_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partition_, dst);
    return written;
  }

  bool operator==(const StopReplicaPartition& rhs) const {
    return topic_ == rhs.topic_ && partition_ == rhs.partition_;
  };

  friend std::ostream& operator<<(std::ostream& os, const StopReplicaPartition& arg) {
    return os << "{topic=" << arg.topic_ << ", partition=" << arg.partition_ << "}";
  }
};

class StopReplicaRequest : public Request {
public:
  // v0
  StopReplicaRequest(INT32 controller_id, INT32 controller_epoch, BOOLEAN delete_partitions,
                     NULLABLE_ARRAY<StopReplicaPartition> partitions)
      : Request{RequestType::StopReplica}, controller_id_{controller_id},
        controller_epoch_{controller_epoch}, delete_partitions_{delete_partitions},
        partitions_{partitions} {};

  bool operator==(const StopReplicaRequest& rhs) const {
    return request_header_ == rhs.request_header_ && controller_id_ == rhs.controller_id_ &&
           controller_epoch_ == rhs.controller_epoch_ &&
           delete_partitions_ == rhs.delete_partitions_ && partitions_ == rhs.partitions_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(controller_id_, dst);
    written += encoder.encode(controller_epoch_, dst);
    written += encoder.encode(delete_partitions_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{controller_id=" << controller_id_ << ", controller_epoch=" << controller_epoch_
              << ", delete_partitions=" << delete_partitions_ << ", partitions=" << partitions_
              << "}";
  }

private:
  const INT32 controller_id_;
  const INT32 controller_epoch_;
  const BOOLEAN delete_partitions_;
  const NULLABLE_ARRAY<StopReplicaPartition> partitions_;
};

// clang-format off
class StopReplicaPartitionV0Buffer : public CompositeBuffer<StopReplicaPartition, StringBuffer, Int32Buffer> {};
class StopReplicaPartitionV0ArrayBuffer : public ArrayBuffer<StopReplicaPartition, StopReplicaPartitionV0Buffer> {};

class StopReplicaRequestV0Buffer : public CompositeBuffer<StopReplicaRequest, Int32Buffer, Int32Buffer, BoolBuffer, StopReplicaPartitionV0ArrayBuffer> {};

DEFINE_REQUEST_PARSER(StopReplicaRequest, V0);
// clang-format on

// === UPDATE METADATA (6) =====================================================

// uses MetadataPartitionState from LeaderAndIsr

struct UpdateMetadataLiveBrokerEndpoint {
  const INT32 port_;
  const STRING host_;
  const STRING listener_name_;
  const INT16 security_protocol_type_;

  // v1 .. v2
  UpdateMetadataLiveBrokerEndpoint(INT32 port, STRING host, INT16 security_protocol_type)
      : UpdateMetadataLiveBrokerEndpoint{port, host, "", security_protocol_type} {};

  // v3
  UpdateMetadataLiveBrokerEndpoint(INT32 port, STRING host, STRING listener_name,
                                   INT16 security_protocol_type)
      : port_{port}, host_{host}, listener_name_{listener_name}, security_protocol_type_{
                                                                     security_protocol_type} {};

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(port_, dst);
    written += encoder.encode(host_, dst);
    if (encoder.apiVersion() >= 3) {
      written += encoder.encode(listener_name_, dst);
    }
    written += encoder.encode(security_protocol_type_, dst);
    return written;
  }

  bool operator==(const UpdateMetadataLiveBrokerEndpoint& rhs) const {
    return port_ == rhs.port_ && host_ == rhs.host_ && listener_name_ == rhs.listener_name_ &&
           security_protocol_type_ == rhs.security_protocol_type_;
  };

  friend std::ostream& operator<<(std::ostream& os, const UpdateMetadataLiveBrokerEndpoint& arg) {
    return os << "{port=" << arg.port_ << ", host=" << arg.host_
              << ", listener_name=" << arg.listener_name_
              << ", security_protocol_type=" << arg.security_protocol_type_ << "}";
  }
};

struct UpdateMetadataLiveBroker {
  const INT32 id_;
  const NULLABLE_ARRAY<UpdateMetadataLiveBrokerEndpoint> endpoints_;
  const NULLABLE_STRING rack_; // since v2

  // v0
  // instead of having dedicated fields, store data as single UpdateMetadataLiveBrokerEndpoint (java
  // client does it as well)
  UpdateMetadataLiveBroker(INT32 id, STRING host, INT32 port)
      : UpdateMetadataLiveBroker(id, {{UpdateMetadataLiveBrokerEndpoint{port, host, 0}}}){};

  // v1
  UpdateMetadataLiveBroker(INT32 id, NULLABLE_ARRAY<UpdateMetadataLiveBrokerEndpoint> endpoints)
      : UpdateMetadataLiveBroker{id, endpoints, absl::nullopt} {};

  // v2
  UpdateMetadataLiveBroker(INT32 id, NULLABLE_ARRAY<UpdateMetadataLiveBrokerEndpoint> endpoints,
                           NULLABLE_STRING rack)
      : id_{id}, endpoints_{endpoints}, rack_{rack} {};

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(id_, dst);
    if (encoder.apiVersion() == 0) {
      // we stored host+port as endpoint, but need to serialize properly
      const UpdateMetadataLiveBrokerEndpoint& only_endpoint = (*endpoints_)[0];
      written += encoder.encode(only_endpoint.host_, dst);
      written += encoder.encode(only_endpoint.port_, dst);
    } else {
      written += encoder.encode(endpoints_, dst);
      if (encoder.apiVersion() >= 2) {
        written += encoder.encode(rack_, dst);
      }
    }
    return written;
  }

  bool operator==(const UpdateMetadataLiveBroker& rhs) const {
    return id_ == rhs.id_ && endpoints_ == rhs.endpoints_ && rack_ == rhs.rack_;
  };

  friend std::ostream& operator<<(std::ostream& os, const UpdateMetadataLiveBroker& arg) {
    return os << "{id=" << arg.id_ << ", endpoints=" << arg.endpoints_ << ", rack=" << arg.rack_
              << "}";
  }
};

class UpdateMetadataRequest : public Request {
public:
  // v0
  UpdateMetadataRequest(INT32 controller_id, INT32 controller_epoch,
                        NULLABLE_ARRAY<MetadataPartitionState> partition_states,
                        NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers)
      : Request{RequestType::UpdateMetadata}, controller_id_{controller_id},
        controller_epoch_{controller_epoch}, partition_states_{partition_states},
        live_brokers_{live_brokers} {};

  bool operator==(const UpdateMetadataRequest& rhs) const {
    return request_header_ == rhs.request_header_ && controller_id_ == rhs.controller_id_ &&
           controller_epoch_ == rhs.controller_epoch_ &&
           partition_states_ == rhs.partition_states_ && live_brokers_ == rhs.live_brokers_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(controller_id_, dst);
    written += encoder.encode(controller_epoch_, dst);
    written += encoder.encode(partition_states_, dst);
    written += encoder.encode(live_brokers_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{controller_id=" << controller_id_ << ", controller_epoch=" << controller_epoch_
              << ", partition_states=" << partition_states_ << ", live_brokers=" << live_brokers_
              << "}";
  }

private:
  const INT32 controller_id_;
  const INT32 controller_epoch_;
  const NULLABLE_ARRAY<MetadataPartitionState> partition_states_;
  const NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers_;
};

// clang-format off
class UpdateMetadataLiveBrokerEndpointV1Buffer : public CompositeBuffer<UpdateMetadataLiveBrokerEndpoint, Int32Buffer, StringBuffer, Int16Buffer> {};
class UpdateMetadataLiveBrokerEndpointV1ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBrokerEndpoint, UpdateMetadataLiveBrokerEndpointV1Buffer> {};
class UpdateMetadataLiveBrokerEndpointV3Buffer : public CompositeBuffer<UpdateMetadataLiveBrokerEndpoint, Int32Buffer, StringBuffer, StringBuffer, Int16Buffer> {};
class UpdateMetadataLiveBrokerEndpointV3ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBrokerEndpoint, UpdateMetadataLiveBrokerEndpointV3Buffer> {};

class UpdateMetadataLiveBrokerV0Buffer : public CompositeBuffer<UpdateMetadataLiveBroker, Int32Buffer, StringBuffer, Int32Buffer> {};
class UpdateMetadataLiveBrokerV0ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBroker, UpdateMetadataLiveBrokerV0Buffer> {};
class UpdateMetadataLiveBrokerV1Buffer : public CompositeBuffer<UpdateMetadataLiveBroker, Int32Buffer, UpdateMetadataLiveBrokerEndpointV1ArrayBuffer> {};
class UpdateMetadataLiveBrokerV1ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBroker, UpdateMetadataLiveBrokerV1Buffer> {};
class UpdateMetadataLiveBrokerV2Buffer : public CompositeBuffer<UpdateMetadataLiveBroker, Int32Buffer, UpdateMetadataLiveBrokerEndpointV1ArrayBuffer, NullableStringBuffer> {};
class UpdateMetadataLiveBrokerV2ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBroker, UpdateMetadataLiveBrokerV2Buffer> {};
class UpdateMetadataLiveBrokerV3Buffer : public CompositeBuffer<UpdateMetadataLiveBroker, Int32Buffer, UpdateMetadataLiveBrokerEndpointV3ArrayBuffer, NullableStringBuffer> {};
class UpdateMetadataLiveBrokerV3ArrayBuffer : public ArrayBuffer<UpdateMetadataLiveBroker, UpdateMetadataLiveBrokerV3Buffer> {};

class UpdateMetadataRequestV0Buffer : public CompositeBuffer<UpdateMetadataRequest, Int32Buffer, Int32Buffer, MetadataPartitionStateV0ArrayBuffer, UpdateMetadataLiveBrokerV0ArrayBuffer> {};
class UpdateMetadataRequestV1Buffer : public CompositeBuffer<UpdateMetadataRequest, Int32Buffer, Int32Buffer, MetadataPartitionStateV0ArrayBuffer, UpdateMetadataLiveBrokerV1ArrayBuffer> {};
class UpdateMetadataRequestV2Buffer : public CompositeBuffer<UpdateMetadataRequest, Int32Buffer, Int32Buffer, MetadataPartitionStateV0ArrayBuffer, UpdateMetadataLiveBrokerV2ArrayBuffer> {};
class UpdateMetadataRequestV3Buffer : public CompositeBuffer<UpdateMetadataRequest, Int32Buffer, Int32Buffer, MetadataPartitionStateV0ArrayBuffer, UpdateMetadataLiveBrokerV3ArrayBuffer> {};

DEFINE_REQUEST_PARSER(UpdateMetadataRequest, V0);
DEFINE_REQUEST_PARSER(UpdateMetadataRequest, V1);
DEFINE_REQUEST_PARSER(UpdateMetadataRequest, V2);
DEFINE_REQUEST_PARSER(UpdateMetadataRequest, V3);
// clang-format on

// === CONTROLLED SHUTDOWN (7) =================================================

// v0 is not documented
class ControlledShutdownRequest : public Request {
public:
  // v1
  ControlledShutdownRequest(INT32 broker_id)
      : Request{RequestType::ControlledShutdown}, broker_id_{broker_id} {};

  bool operator==(const ControlledShutdownRequest& rhs) const {
    return request_header_ == rhs.request_header_ && broker_id_ == rhs.broker_id_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(broker_id_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{broker_id=" << broker_id_ << "}";
  }

private:
  const INT32 broker_id_;
};

// clang-format off
class ControlledShutdownRequestV1Buffer : public CompositeBuffer<ControlledShutdownRequest, Int32Buffer> {};

DEFINE_REQUEST_PARSER(ControlledShutdownRequest, V1);
// clang-format on

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

class OffsetCommitTopicV2ArrayBuffer : public OffsetCommitTopicV0ArrayBuffer {}; // v2 partition format is the same as v0

class OffsetCommitRequestV0Buffer : public CompositeBuffer<OffsetCommitRequest, StringBuffer, OffsetCommitTopicV0ArrayBuffer> {};
class OffsetCommitRequestV1Buffer : public CompositeBuffer<OffsetCommitRequest, StringBuffer, Int32Buffer, StringBuffer, OffsetCommitTopicV1ArrayBuffer> {};
class OffsetCommitRequestV2Buffer : public CompositeBuffer<OffsetCommitRequest, StringBuffer, Int32Buffer, StringBuffer, Int64Buffer, OffsetCommitTopicV2ArrayBuffer> {};

DEFINE_REQUEST_PARSER(OffsetCommitRequest, V0);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V1);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V2);
// clang-format on

// === OFFSET FETCH (9) ========================================================

struct OffsetFetchTopic {
  const STRING topic_;
  const NULLABLE_ARRAY<INT32> partitions_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(topic_, dst);
    written += encoder.encode(partitions_, dst);
    return written;
  }

  bool operator==(const OffsetFetchTopic& rhs) const {
    return topic_ == rhs.topic_ && partitions_ == rhs.partitions_;
  };

  friend std::ostream& operator<<(std::ostream& os, const OffsetFetchTopic& arg) {
    return os << "{topic=" << arg.topic_ << ", partitions=" << arg.partitions_ << "}";
  }
};

class OffsetFetchRequest : public Request {
public:
  // v0 .. v3
  OffsetFetchRequest(STRING group_id, NULLABLE_ARRAY<OffsetFetchTopic> topics)
      : Request{RequestType::OffsetFetch}, group_id_{group_id}, topics_{topics} {};

  bool operator==(const OffsetFetchRequest& rhs) const {
    return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ &&
           topics_ == rhs.topics_;
  };

protected:
  size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
    size_t written{0};
    written += encoder.encode(group_id_, dst);
    written += encoder.encode(topics_, dst);
    return written;
  }

  std::ostream& printDetails(std::ostream& os) const override {
    return os << "{group_id=" << group_id_ << ", topics=" << topics_ << "}";
  }

private:
  const STRING group_id_;
  const NULLABLE_ARRAY<OffsetFetchTopic> topics_;
};

// clang-format off
class OffsetFetchPartitionV0ArrayBuffer : public ArrayBuffer<INT32, Int32Buffer> {};
class OffsetFetchTopicV0Buffer : public CompositeBuffer<OffsetFetchTopic, StringBuffer, OffsetFetchPartitionV0ArrayBuffer> {};
class OffsetFetchTopicV0ArrayBuffer : public ArrayBuffer<OffsetFetchTopic, OffsetFetchTopicV0Buffer> {};

class OffsetFetchRequestV0Buffer : public CompositeBuffer<OffsetFetchRequest, StringBuffer, OffsetFetchTopicV0ArrayBuffer> {};

DEFINE_REQUEST_PARSER(OffsetFetchRequest, V0);
// clang-format on

// === API VERSIONS (18) =======================================================

class ApiVersionsRequest : public Request {
public:
  // v0 .. v1
  ApiVersionsRequest() : Request{RequestType::ApiVersions} {};

  bool operator==(const ApiVersionsRequest& rhs) const {
    return request_header_ == rhs.request_header_;
  };

protected:
  size_t encodeDetails(Buffer::Instance&, EncodingContext&) const override { return 0; }

  std::ostream& printDetails(std::ostream& os) const override { return os << "{}"; }
};

// clang-format off
class ApiVersionsRequestV0Buffer : public NullBuffer<ApiVersionsRequest> {};

DEFINE_REQUEST_PARSER(ApiVersionsRequest, V0);
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
