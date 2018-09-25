#pragma once

#include "extensions/filters/network/kafka/kafka_types.h"

#include "common/common/macros.h"

#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// from http://kafka.apache.org/protocol.html#protocol_api_keys
enum RequestType : INT16 {
  Produce = 0,
  Fetch = 1,
  ListOffsets = 2,
  Metadata = 3,
  LeaderAndIsr = 4,
  StopReplica = 5,
  UpdateMetadata = 6,
  ControlledShutdown = 7,
  OffsetCommit = 8,
  OffsetFetch = 9,
  FindCoordinator = 10,
  JoinGroup = 11,
  Heartbeat = 12,
  LeaveGroup = 13,
  SyncGroup = 14,
  DescribeGroups = 15,
  ListGroups = 16,
  SaslHandshake = 17,
  ApiVersions = 18,
  CreateTopics = 19,
  DeleteTopics = 20,
  DeleteRecords = 21,
  InitProducerId = 22,
  OffsetForLeaderEpoch = 23,
  AddPartitionsToTxn = 24,
  AddOffsetsToTxn = 25,
  EndTxn = 26,
  WriteTxnMarkers = 27,
  TxnOffsetCommit = 28,
  DescribeAcls = 29,
  CreateAcls = 30,
  DeleteAcls = 31,
  DescribeConfigs = 32,
  AlterConfigs = 33,
  AlterReplicaLogDirs = 34,
  DescribeLogDirs = 35,
  SaslAuthenticate = 36,
  CreatePartitions = 37,
  CreateDelegationToken = 38,
  RenewDelegationToken = 39,
  ExpireDelegationToken = 40,
  DescribeDelegationToken = 41,
  DeleteGroups = 42
};

struct RequestSpec {
  const INT16 api_key_;
  const std::string name_;
};

struct KafkaRequest {

  static const std::vector<RequestSpec>& requests() {
    CONSTRUCT_ON_FIRST_USE(
        std::vector<RequestSpec>,
        {RequestType::Produce, "Produce"},
        {RequestType::Fetch, "Fetch"},
        {RequestType::ListOffsets, "ListOffsets"},
        {RequestType::Metadata, "Metadata"},
        {RequestType::LeaderAndIsr, "LeaderAndIsr"},
        {RequestType::StopReplica, "StopReplica"},
        {RequestType::UpdateMetadata, "UpdateMetadata"},
        {RequestType::ControlledShutdown, "ControlledShutdown"},
        {RequestType::OffsetCommit, "OffsetCommit"},
        {RequestType::OffsetFetch, "OffsetFetch"},
        {RequestType::FindCoordinator, "FindCoordinator"},
        {RequestType::JoinGroup, "JoinGroup"},
        {RequestType::Heartbeat, "Heartbeat"},
        {RequestType::LeaveGroup, "LeaveGroup"},
        {RequestType::SyncGroup, "SyncGroup"},
        {RequestType::DescribeGroups, "DescribeGroups"},
        {RequestType::ListGroups, "ListGroups"},
        {RequestType::SaslHandshake, "SaslHandshake"},
        {RequestType::ApiVersions, "ApiVersions"},
        {RequestType::CreateTopics, "CreateTopics"},
        {RequestType::DeleteTopics, "DeleteTopics"},
        {RequestType::DeleteRecords, "DeleteRecords"},
        {RequestType::InitProducerId, "InitProducerId"},
        {RequestType::OffsetForLeaderEpoch, "OffsetForLeaderEpoch"},
        {RequestType::AddPartitionsToTxn, "AddPartitionsToTxn"},
        {RequestType::AddOffsetsToTxn, "AddOffsetsToTxn"},
        {RequestType::EndTxn, "EndTxn"},
        {RequestType::WriteTxnMarkers, "WriteTxnMarkers"},
        {RequestType::TxnOffsetCommit, "TxnOffsetCommit"},
        {RequestType::DescribeAcls, "DescribeAcls"},
        {RequestType::CreateAcls, "CreateAcls"},
        {RequestType::DeleteAcls, "DeleteAcls"},
        {RequestType::DescribeConfigs, "DescribeConfigs"},
        {RequestType::AlterConfigs, "AlterConfigs"},
        {RequestType::AlterReplicaLogDirs, "AlterReplicaLogDirs"},
        {RequestType::DescribeLogDirs, "DescribeLogDirs"},
        {RequestType::SaslAuthenticate, "SaslAuthenticate"},
        {RequestType::CreatePartitions, "CreatePartitions"},
        {RequestType::CreateDelegationToken, "CreateDelegationToken"},
        {RequestType::RenewDelegationToken, "RenewDelegationToken"},
        {RequestType::ExpireDelegationToken, "ExpireDelegationToken"},
        {RequestType::DescribeDelegationToken, "DescribeDelegationToken"},
        {RequestType::DeleteGroups, "DeleteGroups"}
    );
  }

};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
