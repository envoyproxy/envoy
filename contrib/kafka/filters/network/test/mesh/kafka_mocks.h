#pragma once

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
              (RdKafka::Conf&, const std::string&, const std::string&, std::string&), (const));
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfDeliveryCallback,
              (RdKafka::Conf&, RdKafka::DeliveryReportCb*, std::string&), (const));
  MOCK_METHOD((std::unique_ptr<RdKafka::Producer>), createProducer,
              (RdKafka::Conf*, std::string& errstr), (const));
  MOCK_METHOD((std::unique_ptr<RdKafka::KafkaConsumer>), createConsumer,
              (RdKafka::Conf*, std::string& errstr), (const));
  MOCK_METHOD(RdKafka::Headers*, convertHeaders,
              ((const std::vector<std::pair<absl::string_view, absl::string_view>>&)), (const));
  MOCK_METHOD(void, deleteHeaders, (RdKafka::Headers * librdkafka_headers), (const));
  MOCK_METHOD(ConsumerAssignmentConstPtr, assignConsumerPartitions,
              (RdKafka::KafkaConsumer&, const std::string&, int32_t), (const));

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
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(std::string, memberid, (), (const));
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
  MOCK_METHOD(std::string, clusterid, (int), ());
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

class MockKafkaProducer : public RdKafka::Producer, public MockKafkaHandle {
public:
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
  MOCK_METHOD(RdKafka::Error*, sasl_background_callbacks_enable, (), ());
  MOCK_METHOD(RdKafka::Queue*, get_sasl_queue, (), ());
  MOCK_METHOD(RdKafka::Queue*, get_background_queue, (), ());
  MOCK_METHOD(RdKafka::Error*, sasl_set_credentials, (const std::string&, const std::string&), ());
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
  MOCK_METHOD(int32_t, leader_epoch, (), (const));
  MOCK_METHOD(RdKafka::Error*, offset_store, ());
};

class MockKafkaConsumer : public RdKafka::KafkaConsumer, public MockKafkaHandle {
public:
  MOCK_METHOD(RdKafka::ErrorCode, assignment, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, subscription, (std::vector<std::string>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, subscribe, (const std::vector<std::string>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, unsubscribe, (), ());
  MOCK_METHOD(RdKafka::ErrorCode, assign, (const std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, unassign, (), ());
  MOCK_METHOD(RdKafka::Message*, consume, (int), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (RdKafka::Message*), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (RdKafka::Message*), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitAsync, (const std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitSync, (RdKafka::OffsetCommitCb*), ());
  MOCK_METHOD(RdKafka::ErrorCode, commitSync,
              (std::vector<RdKafka::TopicPartition*>&, RdKafka::OffsetCommitCb*), ());
  MOCK_METHOD(RdKafka::ErrorCode, committed, (std::vector<RdKafka::TopicPartition*>&, int), ());
  MOCK_METHOD(RdKafka::ErrorCode, position, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ErrorCode, close, (), ());
  MOCK_METHOD(RdKafka::Error*, close, (RdKafka::Queue*), ());
  MOCK_METHOD(bool, closed, (), ());
  MOCK_METHOD(RdKafka::ErrorCode, seek, (const RdKafka::TopicPartition&, int), ());
  MOCK_METHOD(RdKafka::ErrorCode, offsets_store, (std::vector<RdKafka::TopicPartition*>&), ());
  MOCK_METHOD(RdKafka::ConsumerGroupMetadata*, groupMetadata, (), ());
  MOCK_METHOD(bool, assignment_lost, (), ());
  MOCK_METHOD(std::string, rebalance_protocol, (), ());
  MOCK_METHOD(RdKafka::Error*, incremental_assign, (const std::vector<RdKafka::TopicPartition*>&),
              ());
  MOCK_METHOD(RdKafka::Error*, incremental_unassign, (const std::vector<RdKafka::TopicPartition*>&),
              ());
  MOCK_METHOD(RdKafka::Error*, sasl_background_callbacks_enable, (), ());
  MOCK_METHOD(RdKafka::Queue*, get_sasl_queue, (), ());
  MOCK_METHOD(RdKafka::Queue*, get_background_queue, (), ());
  MOCK_METHOD(RdKafka::Error*, sasl_set_credentials, (const std::string&, const std::string&), ());
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
