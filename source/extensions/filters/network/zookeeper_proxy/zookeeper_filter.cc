#include "extensions/filters/network/zookeeper_proxy/zookeeper_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

ZooKeeperFilterConfig::ZooKeeperFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(scope), stat_prefix_(stat_prefix), stats_(generateStats(stat_prefix, scope)) {}

ZooKeeperFilter::ZooKeeperFilter(ZooKeeperFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

void ZooKeeperFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus ZooKeeperFilter::onData(Buffer::Instance& data, bool) {
  doDecode(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onWrite(Buffer::Instance& data, bool) {
  doDecode(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void ZooKeeperFilter::doDecode(Buffer::Instance& buffer) {
  // Clear dynamic metadata.
  envoy::api::v2::core::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().ZooKeeperProxy];
  metadata.mutable_fields()->clear();

  if (!decoder_) {
    decoder_ = createDecoder(*this);
  }

  try {
    decoder_->onData(buffer);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "zookeeper_proxy: decoding error: {}", e.what());
    config_->stats_.decoder_error_.inc();
  }
}

DecoderPtr ZooKeeperFilter::createDecoder(DecoderCallbacks& callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void ZooKeeperFilter::onConnect(const bool readonly) {
  if (readonly) {
    config_->stats_.connect_readonly_rq_.inc();
  } else {
    config_->stats_.connect_rq_.inc();
  }

  // TODO: set dynamic metadata.
}

void ZooKeeperFilter::onDecodeError() { config_->stats_.decoder_error_.inc(); }

void ZooKeeperFilter::onPing() { config_->stats_.ping_rq_.inc(); }

void ZooKeeperFilter::onAuthRequest(const std::string& scheme) {
  config_->scope_.counter(fmt::format("{}.auth.{}_rq", config_->stat_prefix_, scheme)).inc();
}

void ZooKeeperFilter::onGetDataRequest(const std::string&, const bool) {
  config_->stats_.getdata_rq_.inc();
  // TODO: set metadata.
}

void ZooKeeperFilter::onCreateRequest(const std::string&, const bool, const bool, const bool two) {
  if (!two) {
    config_->stats_.create_rq_.inc();
  } else {
    config_->stats_.create2_rq_.inc();
  }
  // TODO: set metadata.
}

void ZooKeeperFilter::onSetRequest(const std::string&) {
  config_->stats_.setdata_rq_.inc();
  // TODO: set metadata.
}

void ZooKeeperFilter::onGetChildrenRequest(const std::string&, const bool, const bool two) {
  if (!two) {
    config_->stats_.getchildren_rq_.inc();
  } else {
    config_->stats_.getchildren2_rq_.inc();
  }
  // TODO: set metadata.
}

void ZooKeeperFilter::onDeleteRequest(const std::string&, const int32_t) {
  config_->stats_.remove_rq_.inc();
  // TODO: set metadata.
}

void ZooKeeperFilter::onExistsRequest(const std::string&, const bool) {
  config_->stats_.exists_rq_.inc();
  // TODO: set metadata.
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
