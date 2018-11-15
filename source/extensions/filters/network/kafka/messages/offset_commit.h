#pragma once

#include "extensions/filters/network/kafka/kafka_request.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Generic description : http://kafka.apache.org/protocol.html#The_Messages_OffsetCommit
 */

/**
 * Holds the partition node (leaf)
 * Supports all versions (some fields are not used in some versions)
 */
struct OffsetCommitPartition {
  const int32_t partition_;
  const int64_t offset_;
  const int64_t timestamp_; // only v1
  const NullableString metadata_;

  // v0 *and* v2
  OffsetCommitPartition(int32_t partition, int64_t offset, NullableString metadata)
      : partition_{partition}, offset_{offset}, timestamp_{-1}, metadata_{metadata} {};

  // v1
  OffsetCommitPartition(int32_t partition, int64_t offset, int64_t timestamp,
                        NullableString metadata)
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

/**
 * Holds the topic node (contains multiple partitions)
 */
struct OffsetCommitTopic {
  const std::string topic_;
  const NullableArray<OffsetCommitPartition> partitions_;

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

/**
 * Holds the request (contains multiple topics)
 */
class OffsetCommitRequest : public Request {
public:
  // v0
  OffsetCommitRequest(std::string group_id, NullableArray<OffsetCommitTopic> topics)
      : OffsetCommitRequest(group_id, -1, "", -1, topics){};

  // v1
  OffsetCommitRequest(std::string group_id, int32_t group_generation_id, std::string member_id,
                      NullableArray<OffsetCommitTopic> topics)
      : OffsetCommitRequest(group_id, group_generation_id, member_id, -1, topics){};

  // v2 .. v3
  OffsetCommitRequest(std::string group_id, int32_t group_generation_id, std::string member_id,
                      int64_t retention_time, NullableArray<OffsetCommitTopic> topics)
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
  const std::string group_id_;
  const int32_t group_generation_id_; // since v1
  const std::string member_id_;       // since v1
  const int64_t retention_time_;      // since v2
  const NullableArray<OffsetCommitTopic> topics_;
};

/**
 * Deserializes bytes into OffsetCommitPartition (api version 0)
 */
class OffsetCommitPartitionV0Buffer : public Deserializer<OffsetCommitPartition> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += partition_.feed(buffer, remaining);
    consumed += offset_.feed(buffer, remaining);
    consumed += metadata_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return metadata_.ready(); }
  OffsetCommitPartition get() const { return {partition_.get(), offset_.get(), metadata_.get()}; }

protected:
  Int32Deserializer partition_;
  Int64Deserializer offset_;
  NullableStringDeserializer metadata_;
};

/**
 * Deserializes bytes into OffsetCommitPartition (api version 1)
 */
class OffsetCommitPartitionV1Buffer : public Deserializer<OffsetCommitPartition> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += partition_.feed(buffer, remaining);
    consumed += offset_.feed(buffer, remaining);
    consumed += timestamp_.feed(buffer, remaining);
    consumed += metadata_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return metadata_.ready(); }
  OffsetCommitPartition get() const {
    return {partition_.get(), offset_.get(), timestamp_.get(), metadata_.get()};
  }

protected:
  Int32Deserializer partition_;
  Int64Deserializer offset_;
  Int64Deserializer timestamp_;
  NullableStringDeserializer metadata_;
};

/**
 * Deserializes array of OffsetCommitPartition-s v0
 */
class OffsetCommitPartitionV0ArrayBuffer
    : public ArrayDeserializer<OffsetCommitPartition, OffsetCommitPartitionV0Buffer> {};

/**
 * Deserializes array of OffsetCommitPartition-s v1
 */
class OffsetCommitPartitionV1ArrayBuffer
    : public ArrayDeserializer<OffsetCommitPartition, OffsetCommitPartitionV1Buffer> {};

/**
 * Deserializes bytes into OffsetCommitTopic v0 (which is composed of topic name + array of v0
 * partitions)
 */
class OffsetCommitTopicV0Buffer : public Deserializer<OffsetCommitTopic> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += topic_.feed(buffer, remaining);
    consumed += partitions_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return partitions_.ready(); }
  OffsetCommitTopic get() const { return {topic_.get(), partitions_.get()}; }

protected:
  StringDeserializer topic_;
  OffsetCommitPartitionV0ArrayBuffer partitions_;
};

/**
 * Deserializes bytes into OffsetCommitTopic v1 (which is composed of topic name + array of v1
 * partitions)
 */
class OffsetCommitTopicV1Buffer : public Deserializer<OffsetCommitTopic> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += topic_.feed(buffer, remaining);
    consumed += partitions_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return partitions_.ready(); }
  OffsetCommitTopic get() const { return {topic_.get(), partitions_.get()}; }

protected:
  StringDeserializer topic_;
  OffsetCommitPartitionV1ArrayBuffer partitions_;
};

/**
 * Deserializes array of OffsetCommitTopic-s v0
 */
class OffsetCommitTopicV0ArrayBuffer
    : public ArrayDeserializer<OffsetCommitTopic, OffsetCommitTopicV0Buffer> {};

/**
 * Deserializes array of OffsetCommitTopic-s v1
 */
class OffsetCommitTopicV1ArrayBuffer
    : public ArrayDeserializer<OffsetCommitTopic, OffsetCommitTopicV1Buffer> {};

class OffsetCommitRequestV0Buffer : public Deserializer<OffsetCommitRequest> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += group_id_.feed(buffer, remaining);
    consumed += topics_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return topics_.ready(); }
  OffsetCommitRequest get() const { return {group_id_.get(), topics_.get()}; }

protected:
  StringDeserializer group_id_;
  OffsetCommitTopicV0ArrayBuffer topics_;
};

class OffsetCommitRequestV1Buffer : public Deserializer<OffsetCommitRequest> {
public:
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += group_id_.feed(buffer, remaining);
    consumed += generation_id_.feed(buffer, remaining);
    consumed += member_id_.feed(buffer, remaining);
    consumed += topics_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return topics_.ready(); }
  OffsetCommitRequest get() const {
    return {group_id_.get(), generation_id_.get(), member_id_.get(), topics_.get()};
  }

protected:
  StringDeserializer group_id_;
  Int32Deserializer generation_id_;
  StringDeserializer member_id_;
  OffsetCommitTopicV1ArrayBuffer topics_;
};

/**
 * Define Parsers that wrap the corresponding buffers
 */

DEFINE_REQUEST_PARSER(OffsetCommitRequest, V0);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V1);

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
