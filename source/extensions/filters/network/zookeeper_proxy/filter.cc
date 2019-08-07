#include "extensions/filters/network/zookeeper_proxy/filter.h"

#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

ZooKeeperFilterConfig::ZooKeeperFilterConfig(const std::string& stat_prefix,
                                             const uint32_t max_packet_bytes, Stats::Scope& scope)
    : scope_(scope), max_packet_bytes_(max_packet_bytes), stat_prefix_(stat_prefix),
      stats_(generateStats(stat_prefix, scope)) {}

ZooKeeperFilter::ZooKeeperFilter(ZooKeeperFilterConfigSharedPtr config)
    : config_(std::move(config)), decoder_(createDecoder(*this)) {}

void ZooKeeperFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus ZooKeeperFilter::onData(Buffer::Instance& data, bool) {
  clearDynamicMetadata();
  decoder_->onData(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onWrite(Buffer::Instance& data, bool) {
  clearDynamicMetadata();
  decoder_->onWrite(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onNewConnection() { return Network::FilterStatus::Continue; }

DecoderPtr ZooKeeperFilter::createDecoder(DecoderCallbacks& callbacks) {
  return std::make_unique<DecoderImpl>(callbacks, config_->maxPacketBytes());
}

void ZooKeeperFilter::setDynamicMetadata(const std::string& key, const std::string& value) {
  setDynamicMetadata({{key, value}});
}

void ZooKeeperFilter::clearDynamicMetadata() {
  envoy::api::v2::core::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().ZooKeeperProxy];
  metadata.mutable_fields()->clear();
}

void ZooKeeperFilter::setDynamicMetadata(
    const std::vector<std::pair<const std::string, const std::string>>& data) {
  envoy::api::v2::core::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  ProtobufWkt::Struct metadata(
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().ZooKeeperProxy]);
  auto& fields = *metadata.mutable_fields();

  for (const auto& pair : data) {
    auto val = ProtobufWkt::Value();
    val.set_string_value(pair.second);
    fields.insert({pair.first, val});
  }

  read_callbacks_->connection().streamInfo().setDynamicMetadata(
      NetworkFilterNames::get().ZooKeeperProxy, metadata);
}

void ZooKeeperFilter::onConnect(const bool readonly) {
  if (readonly) {
    config_->stats_.connect_readonly_rq_.inc();
    setDynamicMetadata("opname", "connect_readonly");
  } else {
    config_->stats_.connect_rq_.inc();
    setDynamicMetadata("opname", "connect");
  }
}

void ZooKeeperFilter::onDecodeError() {
  config_->stats_.decoder_error_.inc();
  setDynamicMetadata("opname", "error");
}

void ZooKeeperFilter::onRequestBytes(const uint64_t bytes) {
  config_->stats_.request_bytes_.add(bytes);
  setDynamicMetadata("bytes", std::to_string(bytes));
}

void ZooKeeperFilter::onResponseBytes(const uint64_t bytes) {
  config_->stats_.response_bytes_.add(bytes);
  setDynamicMetadata("bytes", std::to_string(bytes));
}

void ZooKeeperFilter::onPing() {
  config_->stats_.ping_rq_.inc();
  setDynamicMetadata("opname", "ping");
}

void ZooKeeperFilter::onAuthRequest(const std::string& scheme) {
  config_->scope_.counter(fmt::format("{}.auth.{}_rq", config_->stat_prefix_, scheme)).inc();
  setDynamicMetadata("opname", "auth");
}

void ZooKeeperFilter::onGetDataRequest(const std::string& path, const bool watch) {
  config_->stats_.getdata_rq_.inc();
  setDynamicMetadata({{"opname", "getdata"}, {"path", path}, {"watch", watch ? "true" : "false"}});
}

void ZooKeeperFilter::onCreateRequest(const std::string& path, const CreateFlags flags,
                                      const OpCodes opcode) {
  std::string opname;

  switch (opcode) {
  case OpCodes::CREATE:
    opname = "create";
    config_->stats_.create_rq_.inc();
    break;
  case OpCodes::CREATE2:
    opname = "create2";
    config_->stats_.create2_rq_.inc();
    break;
  case OpCodes::CREATECONTAINER:
    opname = "createcontainer";
    config_->stats_.createcontainer_rq_.inc();
    break;
  case OpCodes::CREATETTL:
    opname = "createttl";
    config_->stats_.createttl_rq_.inc();
    break;
  default:
    throw EnvoyException(fmt::format("Unknown opcode: {}", enumToSignedInt(opcode)));
    break;
  }

  setDynamicMetadata(
      {{"opname", opname}, {"path", path}, {"create_type", createFlagsToString(flags)}});
}

void ZooKeeperFilter::onSetRequest(const std::string& path) {
  config_->stats_.setdata_rq_.inc();
  setDynamicMetadata({{"opname", "setdata"}, {"path", path}});
}

void ZooKeeperFilter::onGetChildrenRequest(const std::string& path, const bool watch,
                                           const bool v2) {
  std::string opname = "getchildren";

  if (v2) {
    config_->stats_.getchildren2_rq_.inc();
    opname = "getchildren2";
  } else {
    config_->stats_.getchildren_rq_.inc();
  }

  setDynamicMetadata({{"opname", opname}, {"path", path}, {"watch", watch ? "true" : "false"}});
}

void ZooKeeperFilter::onDeleteRequest(const std::string& path, const int32_t version) {
  config_->stats_.delete_rq_.inc();
  setDynamicMetadata({{"opname", "delete"}, {"path", path}, {"version", std::to_string(version)}});
}

void ZooKeeperFilter::onExistsRequest(const std::string& path, const bool watch) {
  config_->stats_.exists_rq_.inc();
  setDynamicMetadata({{"opname", "exists"}, {"path", path}, {"watch", watch ? "true" : "false"}});
}

void ZooKeeperFilter::onGetAclRequest(const std::string& path) {
  config_->stats_.getacl_rq_.inc();
  setDynamicMetadata({{"opname", "getacl"}, {"path", path}});
}

void ZooKeeperFilter::onSetAclRequest(const std::string& path, const int32_t version) {
  config_->stats_.setacl_rq_.inc();
  setDynamicMetadata({{"opname", "setacl"}, {"path", path}, {"version", std::to_string(version)}});
}

void ZooKeeperFilter::onSyncRequest(const std::string& path) {
  config_->stats_.sync_rq_.inc();
  setDynamicMetadata({{"opname", "sync"}, {"path", path}});
}

void ZooKeeperFilter::onCheckRequest(const std::string&, const int32_t) {
  config_->stats_.check_rq_.inc();
}

void ZooKeeperFilter::onCheckWatchesRequest(const std::string& path, const int32_t) {
  config_->stats_.checkwatches_rq_.inc();
  setDynamicMetadata({{"opname", "checkwatches"}, {"path", path}});
}

void ZooKeeperFilter::onRemoveWatchesRequest(const std::string& path, const int32_t) {
  config_->stats_.removewatches_rq_.inc();
  setDynamicMetadata({{"opname", "removewatches"}, {"path", path}});
}

void ZooKeeperFilter::onMultiRequest() {
  config_->stats_.multi_rq_.inc();
  setDynamicMetadata("opname", "multi");
}

void ZooKeeperFilter::onReconfigRequest() {
  config_->stats_.reconfig_rq_.inc();
  setDynamicMetadata("opname", "reconfig");
}

void ZooKeeperFilter::onSetWatchesRequest() {
  config_->stats_.setwatches_rq_.inc();
  setDynamicMetadata("opname", "setwatches");
}

void ZooKeeperFilter::onGetEphemeralsRequest(const std::string& path) {
  config_->stats_.getephemerals_rq_.inc();
  setDynamicMetadata({{"opname", "getephemerals"}, {"path", path}});
}

void ZooKeeperFilter::onGetAllChildrenNumberRequest(const std::string& path) {
  config_->stats_.getallchildrennumber_rq_.inc();
  setDynamicMetadata({{"opname", "getallchildrennumber"}, {"path", path}});
}

void ZooKeeperFilter::onCloseRequest() {
  config_->stats_.close_rq_.inc();
  setDynamicMetadata("opname", "close");
}

void ZooKeeperFilter::onConnectResponse(const int32_t proto_version, const int32_t timeout,
                                        const bool readonly) {
  config_->stats_.connect_resp_.inc();
  setDynamicMetadata({{"opname", "connect_response"},
                      {"protocol_version", std::to_string(proto_version)},
                      {"timeout", std::to_string(timeout)},
                      {"readonly", std::to_string(readonly)}});
}

void ZooKeeperFilter::onResponse(const OpCodes opcode, const int32_t xid, const int64_t zxid,
                                 const int32_t error) {
  std::string opname = "";

  switch (opcode) {
  case OpCodes::PING:
    config_->stats_.ping_resp_.inc();
    opname = "ping_response";
    break;
  case OpCodes::SETAUTH:
    config_->stats_.auth_resp_.inc();
    opname = "auth_response";
    break;
  case OpCodes::GETDATA:
    opname = "getdata_resp";
    config_->stats_.getdata_resp_.inc();
    break;
  case OpCodes::CREATE:
    opname = "create_resp";
    config_->stats_.create_resp_.inc();
    break;
  case OpCodes::CREATE2:
    opname = "create2_resp";
    config_->stats_.create2_resp_.inc();
    break;
  case OpCodes::CREATECONTAINER:
    opname = "createcontainer_resp";
    config_->stats_.createcontainer_resp_.inc();
    break;
  case OpCodes::CREATETTL:
    opname = "createttl_resp";
    config_->stats_.createttl_resp_.inc();
    break;
  case OpCodes::SETDATA:
    opname = "setdata_resp";
    config_->stats_.setdata_resp_.inc();
    break;
  case OpCodes::GETCHILDREN:
    opname = "getchildren_resp";
    config_->stats_.getchildren_resp_.inc();
    break;
  case OpCodes::GETCHILDREN2:
    opname = "getchildren2_resp";
    config_->stats_.getchildren2_resp_.inc();
    break;
  case OpCodes::DELETE:
    opname = "delete_resp";
    config_->stats_.delete_resp_.inc();
    break;
  case OpCodes::EXISTS:
    opname = "exists_resp";
    config_->stats_.exists_resp_.inc();
    break;
  case OpCodes::GETACL:
    config_->stats_.getacl_resp_.inc();
    opname = "getacl_resp";
    break;
  case OpCodes::SETACL:
    opname = "setacl_resp";
    config_->stats_.setacl_resp_.inc();
    break;
  case OpCodes::SYNC:
    opname = "sync_resp";
    config_->stats_.sync_resp_.inc();
    break;
  case OpCodes::CHECK:
    opname = "check_resp";
    config_->stats_.check_resp_.inc();
    break;
  case OpCodes::MULTI:
    opname = "multi_resp";
    config_->stats_.multi_resp_.inc();
    break;
  case OpCodes::RECONFIG:
    opname = "reconfig_resp";
    config_->stats_.reconfig_resp_.inc();
    break;
  case OpCodes::SETWATCHES:
    opname = "setwatches_resp";
    config_->stats_.setwatches_resp_.inc();
    break;
  case OpCodes::CHECKWATCHES:
    opname = "checkwatches_resp";
    config_->stats_.checkwatches_resp_.inc();
    break;
  case OpCodes::REMOVEWATCHES:
    opname = "removewatches_resp";
    config_->stats_.removewatches_resp_.inc();
    break;
  case OpCodes::GETEPHEMERALS:
    opname = "getephemerals_resp";
    config_->stats_.getephemerals_resp_.inc();
    break;
  case OpCodes::GETALLCHILDRENNUMBER:
    opname = "getallchildrennumber_resp";
    config_->stats_.getallchildrennumber_resp_.inc();
    break;
  case OpCodes::CLOSE:
    opname = "close_resp";
    config_->stats_.close_resp_.inc();
    break;
  default:
    break;
  }

  setDynamicMetadata({{"opname", opname},
                      {"xid", std::to_string(xid)},
                      {"zxid", std::to_string(zxid)},
                      {"error", std::to_string(error)}});
}

void ZooKeeperFilter::onWatchEvent(const int32_t event_type, const int32_t client_state,
                                   const std::string& path, const int64_t zxid,
                                   const int32_t error) {
  config_->stats_.watch_event_.inc();
  setDynamicMetadata({{"opname", "watch_event"},
                      {"event_type", std::to_string(event_type)},
                      {"client_state", std::to_string(client_state)},
                      {"path", path},
                      {"zxid", std::to_string(zxid)},
                      {"error", std::to_string(error)}});
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
