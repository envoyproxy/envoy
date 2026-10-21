#pragma once

#include <cstdint>

#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"
#include "gmock/gmock.h"
#include "librdkafka/rdkafkacpp.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// This file defines all librdkafka-related mocks.

class MockConsumerAssignment : public ConsumerAssignment {};

class MockLibRdKafkaUtils : public LibRdKafkaUtils {
public:
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfProperty,
              (RdKafka::Conf&, const std::string&, const std::string&, std::string&),
              (const, override));
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfDeliveryCallback,
              (RdKafka::Conf&, RdKafka::DeliveryReportCb*, std::string&), (const, override));
  MOCK_METHOD((std::unique_ptr<RdKafka::Producer>), createProducer,
              (RdKafka::Conf*, std::string& errstr), (const, override));
  MOCK_METHOD((std::unique_ptr<RdKafka::KafkaConsumer>), createConsumer,
              (RdKafka::Conf*, std::string& errstr), (const, override));
  MOCK_METHOD(RdKafka::Headers*, convertHeaders,
              ((const std::vector<std::pair<absl::string_view, absl::string_view>>&)),
              (const, override));
  MOCK_METHOD(void, deleteHeaders, (RdKafka::Headers * librdkafka_headers), (const, override));
  MOCK_METHOD(ConsumerAssignmentConstPtr, assignConsumerPartitions,
              (RdKafka::KafkaConsumer&, const std::string&, int32_t), (const, override));

  MockLibRdKafkaUtils() {
    ON_CALL(*this, convertHeaders(testing::_))
        .WillByDefault(testing::Return(headers_holder_.get()));
  }

private:
  std::unique_ptr<RdKafka::Headers> headers_holder_{RdKafka::Headers::create()};
};

// Base class for librdkafka objects.
class MockKafkaHandle : public virtual RdKafka::Handle {
public:
  MOCK_METHOD(std::string, name, (), (const, override));
  MOCK_METHOD(std::string, memberid, (), (const, override));
  MOCK_METHOD(int, poll, (int), (override));
  MOCK_METHOD(int, outq_len, (), (override));
  MOCK_METHOD(RdKafka::ErrorCode, metadata,
              (bool, const RdKafka::Topic*, RdKafka::Metadata**, int timout_ms), (override));
  MOCK_METHOD(RdKafka::ErrorCode, pause, (std::vector<RdKafka::TopicPartition*>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, resume, (std::vector<RdKafka::TopicPartition*>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, query_watermark_offsets,
              (const std::string&, int32_t, int64_t*, int64_t*, int), (override));
  MOCK_METHOD(RdKafka::ErrorCode, get_watermark_offsets,
              (const std::string&, int32_t, int64_t*, int64_t*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, offsetsForTimes, (std::vector<RdKafka::TopicPartition*>&, int),
              (override));
  MOCK_METHOD(RdKafka::Queue*, get_partition_queue, (const RdKafka::TopicPartition*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, set_log_queue, (RdKafka::Queue*), (override));
  MOCK_METHOD(void, yield, (), (override));
  MOCK_METHOD(std::string, clusterid, (int), (override));
  MOCK_METHOD(struct rd_kafka_s*, c_ptr, (), (override));
  MOCK_METHOD(int32_t, controllerid, (int), (override));
  MOCK_METHOD(RdKafka::ErrorCode, fatal_error, (std::string&), (const, override));
  MOCK_METHOD(RdKafka::ErrorCode, oauthbearer_set_token,
              (const std::string&, int64_t, const std::string&, const std::list<std::string>&,
               std::string&),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, oauthbearer_set_token_failure, (const std::string&), (override));
  MOCK_METHOD(void*, mem_malloc, (size_t), (override));
  MOCK_METHOD(void, mem_free, (void*), (override));
};

class MockKafkaProducer : public RdKafka::Producer, public MockKafkaHandle {
public:
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, int, void*, size_t, const std::string*, void*),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, int, void*, size_t, const void*, size_t, void*),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t, void*),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t,
               RdKafka::Headers*, void*),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (RdKafka::Topic*, int32_t, const std::vector<char>*, const std::vector<char>*, void*),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, flush, (int), (override));
  MOCK_METHOD(RdKafka::ErrorCode, purge, (int), (override));
  MOCK_METHOD(RdKafka::Error*, init_transactions, (int), (override));
  MOCK_METHOD(RdKafka::Error*, begin_transaction, (), (override));
  MOCK_METHOD(RdKafka::Error*, send_offsets_to_transaction,
              (const std::vector<RdKafka::TopicPartition*>&, const RdKafka::ConsumerGroupMetadata*,
               int),
              (override));
  MOCK_METHOD(RdKafka::Error*, commit_transaction, (int), (override));
  MOCK_METHOD(RdKafka::Error*, abort_transaction, (int), (override));
  MOCK_METHOD(RdKafka::Error*, sasl_background_callbacks_enable, (), (override));
  MOCK_METHOD(RdKafka::Queue*, get_sasl_queue, (), (override));
  MOCK_METHOD(RdKafka::Queue*, get_background_queue, (), (override));
  MOCK_METHOD(RdKafka::Error*, sasl_set_credentials, (const std::string&, const std::string&),
              (override));
};

class MockKafkaMessage : public RdKafka::Message {
public:
  MOCK_METHOD(std::string, errstr, (), (const, override));
  MOCK_METHOD(RdKafka::ErrorCode, err, (), (const, override));
  MOCK_METHOD(RdKafka::Topic*, topic, (), (const, override));
  MOCK_METHOD(std::string, topic_name, (), (const, override));
  MOCK_METHOD(int32_t, partition, (), (const, override));
  MOCK_METHOD(void*, payload, (), (const, override));
  MOCK_METHOD(size_t, len, (), (const, override));
  MOCK_METHOD(const std::string*, key, (), (const, override));
  MOCK_METHOD(const void*, key_pointer, (), (const, override));
  MOCK_METHOD(size_t, key_len, (), (const, override));
  MOCK_METHOD(int64_t, offset, (), (const, override));
  MOCK_METHOD(RdKafka::MessageTimestamp, timestamp, (), (const, override));
  MOCK_METHOD(void*, msg_opaque, (), (const, override));
  MOCK_METHOD(int64_t, latency, (), (const, override));
  MOCK_METHOD(struct rd_kafka_message_s*, c_ptr, (), (override));
  MOCK_METHOD(RdKafka::Message::Status, status, (), (const, override));
  MOCK_METHOD(RdKafka::Headers*, headers, (), (override));
  MOCK_METHOD(RdKafka::Headers*, headers, (RdKafka::ErrorCode*), (override));
  MOCK_METHOD(int32_t, broker_id, (), (const, override));
  MOCK_METHOD(int32_t, leader_epoch, (), (const, override));
  MOCK_METHOD(RdKafka::Error*, offset_store, (), (override));
};

class MockKafkaConsumer : public RdKafka::KafkaConsumer, public MockKafkaHandle {
public:
  MOCK_METHOD(RdKafka::ErrorCode, assignment, (std::vector<RdKafka::TopicPartition*>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, subscription, (std::vector<std::string>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, subscribe, (const std::vector<std::string>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, unsubscribe, (), (override));
  MOCK_METHOD(RdKafka::ErrorCode, assign, (const std::vector<RdKafka::TopicPartition*>&),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, unassign, (), (override));
  MOCK_METHOD(RdKafka::Message*, consume, (int), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (RdKafka::Message*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (RdKafka::Message*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (std::vector<RdKafka::TopicPartition*>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (const std::vector<RdKafka::TopicPartition*>&),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (RdKafka::OffsetCommitCb*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, commitSync,
              (std::vector<RdKafka::TopicPartition*>&, RdKafka::OffsetCommitCb*), (override));
  MOCK_METHOD(RdKafka::ErrorCode, committed, (std::vector<RdKafka::TopicPartition*>&, int),
              (override));
  MOCK_METHOD(RdKafka::ErrorCode, position, (std::vector<RdKafka::TopicPartition*>&), (override));
  MOCK_METHOD(RdKafka::ErrorCode, close, (), (override));
  MOCK_METHOD(RdKafka::Error*, close, (RdKafka::Queue*), (override));
  MOCK_METHOD(bool, closed, (), (override));
  MOCK_METHOD(RdKafka::ErrorCode, seek, (const RdKafka::TopicPartition&, int), (override));
  MOCK_METHOD(RdKafka::ErrorCode, offsets_store, (std::vector<RdKafka::TopicPartition*>&),
              (override));
  MOCK_METHOD(RdKafka::ConsumerGroupMetadata*, groupMetadata, (), (override));
  MOCK_METHOD(bool, assignment_lost, (), (override));
  MOCK_METHOD(std::string, rebalance_protocol, (), (override));
  MOCK_METHOD(RdKafka::Error*, incremental_assign, (const std::vector<RdKafka::TopicPartition*>&),
              (override));
  MOCK_METHOD(RdKafka::Error*, incremental_unassign, (const std::vector<RdKafka::TopicPartition*>&),
              (override));
  MOCK_METHOD(RdKafka::Error*, sasl_background_callbacks_enable, (), (override));
  MOCK_METHOD(RdKafka::Queue*, get_sasl_queue, (), (override));
  MOCK_METHOD(RdKafka::Queue*, get_background_queue, (), (override));
  MOCK_METHOD(RdKafka::Error*, sasl_set_credentials, (const std::string&, const std::string&),
              (override));
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
