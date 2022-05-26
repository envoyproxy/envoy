#pragma once

#include "gmock/gmock.h"
#include "librdkafka/rdkafkacpp.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MockKafkaProducer : public RdKafka::Producer {
public:
  // Producer API.
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, int, void*, size_t, const std::string*, void*), ());
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, int, void*, size_t, const void*, size_t, void*), ());
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t, void*),
              ());
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t,
               RdKafka::Headers*, void*),
              ());
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, const std::vector<char>*, const std::vector<char>*, void*),
              ());
  MOCK_METHOD(RdKafka::ErrorCode, flush, (int), ());
  MOCK_METHOD(RdKafka::ErrorCode, purge, (int), ());
  MOCK_METHOD(RdKafka::Error*, init_transactions, (int), ());
  MOCK_METHOD(RdKafka::Error*, begin_transaction, (), ());
  MOCK_METHOD(RdKafka::Error*, send_offsets_to_transaction,
              (const std::vector<RdKafka::TopicPartition*>&, const RdKafka::ConsumerGroupMetadata*,
               int),
              ());
  MOCK_METHOD(RdKafka::Error*, commit_transaction, (int), ());
  MOCK_METHOD(RdKafka::Error*, abort_transaction, (int), ());

  // Handle API (unused by us).
  MOCK_METHOD(const std::string, name, (), (const));
  MOCK_METHOD(const std::string, memberid, (), (const));
  MOCK_METHOD(int, poll, (int), ());
  MOCK_METHOD(int, outq_len, (), ());
  MOCK_METHOD(RdKafka::ErrorCode, metadata,
              (bool, const RdKafka::Topic*, RdKafka::Metadata**, int timout_ms), ());
  MOCK_METHOD(RdKafka::ErrorCode, pause, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, resume, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, query_watermark_offsets,
              (const std::string&, int32_t, int64_t*, int64_t*, int), ());
  MOCK_METHOD(RdKafka::ErrorCode, get_watermark_offsets,
              (const std::string&, int32_t, int64_t*, int64_t*), ());
  MOCK_METHOD(RdKafka::ErrorCode, offsetsForTimes, (std::vector<RdKafka::TopicPartition*>&, int),
              ());
  MOCK_METHOD(RdKafka::Queue*, get_partition_queue, (const RdKafka::TopicPartition*), ());
  MOCK_METHOD(RdKafka::ErrorCode, set_log_queue, (RdKafka::Queue*), ());
  MOCK_METHOD(void, yield, (), ());
  MOCK_METHOD(const std::string, clusterid, (int), ());
  MOCK_METHOD(struct rd_kafka_s*, c_ptr, (), ());
  MOCK_METHOD(int32_t, controllerid, (int), ());
  MOCK_METHOD(RdKafka::ErrorCode, fatal_error, (std::string&), (const));
  MOCK_METHOD(RdKafka::ErrorCode, oauthbearer_set_token,
              (const std::string&, int64_t, const std::string&, const std::list<std::string>&,
               std::string&),
              ());
  MOCK_METHOD(RdKafka::ErrorCode, oauthbearer_set_token_failure, (const std::string&), ());
  MOCK_METHOD(void*, mem_malloc, (size_t), ());
  MOCK_METHOD(void, mem_free, (void*), ());
};

class MockKafkaMessage : public RdKafka::Message {
public:
  MOCK_METHOD(std::string, errstr, (), (const));
  MOCK_METHOD(RdKafka::ErrorCode, err, (), (const));
  MOCK_METHOD(RdKafka::Topic*, topic, (), (const));
  MOCK_METHOD(std::string, topic_name, (), (const));
  MOCK_METHOD(int32_t, partition, (), (const));
  MOCK_METHOD(void*, payload, (), (const));
  MOCK_METHOD(size_t, len, (), (const));
  MOCK_METHOD(const std::string*, key, (), (const));
  MOCK_METHOD(const void*, key_pointer, (), (const));
  MOCK_METHOD(size_t, key_len, (), (const));
  MOCK_METHOD(int64_t, offset, (), (const));
  MOCK_METHOD(RdKafka::MessageTimestamp, timestamp, (), (const));
  MOCK_METHOD(void*, msg_opaque, (), (const));
  MOCK_METHOD(int64_t, latency, (), (const));
  MOCK_METHOD(struct rd_kafka_message_s*, c_ptr, ());
  MOCK_METHOD(RdKafka::Message::Status, status, (), (const));
  MOCK_METHOD(RdKafka::Headers*, headers, ());
  MOCK_METHOD(RdKafka::Headers*, headers, (RdKafka::ErrorCode*));
  MOCK_METHOD(int32_t, broker_id, (), (const));
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
