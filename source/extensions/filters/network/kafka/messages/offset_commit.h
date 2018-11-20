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
 * Holds the partition data: partition, offset, timestamp, metadata
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
 * Holds the topic data: topic name and partitions in that topic
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
 * Holds the request: group id, generation id, member id, retention time, topics
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

// clang-format off

// api version 0

// Deserializes bytes into OffsetCommitPartition (api version 0): partition, offset, metadata
class OffsetCommitPartitionV0Buffer
    : public CompositeDeserializerWith3Delegates<OffsetCommitPartition, Int32Deserializer, Int64Deserializer, NullableStringDeserializer> {};
// Deserializes array of OffsetCommitPartition-s v0
class OffsetCommitPartitionV0ArrayBuffer
    : public ArrayDeserializer<OffsetCommitPartition, OffsetCommitPartitionV0Buffer> {};
// Deserializes bytes into OffsetCommitTopic (api version 0): topic name, partitions (v0)
class OffsetCommitTopicV0Buffer
    : public CompositeDeserializerWith2Delegates<OffsetCommitTopic, StringDeserializer, OffsetCommitPartitionV0ArrayBuffer> {};
// Deserializes array of OffsetCommitTopic-s v0
class OffsetCommitTopicV0ArrayBuffer
    : public ArrayDeserializer<OffsetCommitTopic, OffsetCommitTopicV0Buffer> {};
// Deserializes bytes into OffsetCommitRequest (api version 0): group_id, topics (v0)
class OffsetCommitRequestV0Deserializer
    : public CompositeDeserializerWith2Delegates<OffsetCommitRequest, StringDeserializer, OffsetCommitTopicV0ArrayBuffer> {};

// api version 1

// Deserializes bytes into OffsetCommitPartition (api version 1): partition, offset, timestamp, metadata
class OffsetCommitPartitionV1Buffer
    : public CompositeDeserializerWith4Delegates<OffsetCommitPartition, Int32Deserializer, Int64Deserializer, Int64Deserializer, NullableStringDeserializer> {};
// Deserializes array of OffsetCommitPartition-s v1
class OffsetCommitPartitionV1ArrayBuffer
    : public ArrayDeserializer<OffsetCommitPartition, OffsetCommitPartitionV1Buffer> {};
// Deserializes bytes into OffsetCommitTopic (api version 1): topic name, partitions (v1)
class OffsetCommitTopicV1Buffer
    : public CompositeDeserializerWith2Delegates<OffsetCommitTopic, StringDeserializer, OffsetCommitPartitionV1ArrayBuffer> {};
// Deserializes array of OffsetCommitTopic-s v1
class OffsetCommitTopicV1ArrayBuffer
    : public ArrayDeserializer<OffsetCommitTopic, OffsetCommitTopicV1Buffer> {};
// Deserializes bytes into OffsetCommitRequest (api version 1): group_id, generation_id, member_id, topics (v1)
class OffsetCommitRequestV1Deserializer
    : public CompositeDeserializerWith4Delegates<OffsetCommitRequest, StringDeserializer, Int32Deserializer, StringDeserializer, OffsetCommitTopicV1ArrayBuffer> {};

// clang-format on

/**
 * Define Parsers that wrap the corresponding deserializers
 */

DEFINE_REQUEST_PARSER(OffsetCommitRequest, V0);
DEFINE_REQUEST_PARSER(OffsetCommitRequest, V1);

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
