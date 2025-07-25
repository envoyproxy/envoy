#pragma once

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/factory_base.h"

#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.pb.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.pb.validate.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

using ReadDrainerConfig =
    test::extensions::filters::udp::udp_proxy::session_filters::DrainerUdpSessionReadFilterConfig;
using WriteDrainerConfig =
    test::extensions::filters::udp::udp_proxy::session_filters::DrainerUdpSessionWriteFilterConfig;
using DrainerConfig =
    test::extensions::filters::udp::udp_proxy::session_filters::DrainerUdpSessionFilterConfig;

using Filter = Network::UdpSessionFilter;
using ReadFilter = Network::UdpSessionReadFilter;
using WriteFilter = Network::UdpSessionWriteFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using WriteFilterStatus = Network::UdpSessionWriteFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using WriteFilterCallbacks = Network::UdpSessionWriteFilterCallbacks;

class DrainerUdpSessionReadFilter : public virtual ReadFilter {
public:
  DrainerUdpSessionReadFilter(int downstream_bytes_to_drain, bool stop_iteration_on_new_session,
                              bool stop_iteration_on_first_read, bool continue_filter_chain)
      : downstream_bytes_to_drain_(downstream_bytes_to_drain),
        stop_iteration_on_new_session_(stop_iteration_on_new_session),
        stop_iteration_on_first_read_(stop_iteration_on_first_read),
        continue_filter_chain_(continue_filter_chain) {}

  void onSessionCompleteInternal() override {
    read_callbacks_->streamInfo().filterState()->setData(
        "test.udp_session.drainer.on_session_complete",
        std::make_shared<Envoy::Router::StringAccessorImpl>("session_complete"),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection,
        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  }

  ReadFilterStatus onNewSession() override {
    if (stop_iteration_on_new_session_) {
      // We can count how many times onNewSession was called on a filter chain
      // by increasing the original bytes to drain value.
      downstream_bytes_to_drain_++;
      return ReadFilterStatus::StopIteration;
    } else {
      // Make sure that we are able to call continueFilterChain() here without
      // impacting the filter chain iteration.
      read_callbacks_->continueFilterChain();
      return ReadFilterStatus::Continue;
    }
  }

  ReadFilterStatus onData(Network::UdpRecvData& data) override {
    data.buffer_->drain(downstream_bytes_to_drain_);
    if (stop_iteration_on_first_read_) {
      stop_iteration_on_first_read_ = false;
      return ReadFilterStatus::StopIteration;
    } else {
      if (continue_filter_chain_) {
        read_callbacks_->continueFilterChain();
        // Calling twice, to make sure that the second call does not have effect.
        read_callbacks_->continueFilterChain();
      }
      return ReadFilterStatus::Continue;
    }
  }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    session_id_ = callbacks.sessionId();
  }

private:
  int downstream_bytes_to_drain_;
  bool stop_iteration_on_new_session_{false};
  bool stop_iteration_on_first_read_{false};
  bool continue_filter_chain_{false};
  uint64_t session_id_;
  ReadFilterCallbacks* read_callbacks_;
};

class DrainerUdpSessionReadFilterConfigFactory : public FactoryBase<ReadDrainerConfig> {
public:
  DrainerUdpSessionReadFilterConfigFactory() : FactoryBase("test.udp_session.drainer_read") {}

private:
  FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ReadDrainerConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addReadFilter(std::make_unique<DrainerUdpSessionReadFilter>(
          config.downstream_bytes_to_drain(), config.stop_iteration_on_new_session(),
          config.stop_iteration_on_first_read(), config.continue_filter_chain()));
    };
  }
};

static Registry::RegisterFactory<DrainerUdpSessionReadFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_drainer_udp_session_read_filter_;

class DrainerUdpSessionWriteFilter : public virtual WriteFilter {
public:
  DrainerUdpSessionWriteFilter(int upstream_bytes_to_drain, bool stop_iteration_on_first_write)
      : upstream_bytes_to_drain_(upstream_bytes_to_drain),
        stop_iteration_on_first_write_(stop_iteration_on_first_write) {}

  WriteFilterStatus onWrite(Network::UdpRecvData& data) override {
    data.buffer_->drain(upstream_bytes_to_drain_);
    if (stop_iteration_on_first_write_) {
      stop_iteration_on_first_write_ = false;
      return WriteFilterStatus::StopIteration;
    } else {
      return WriteFilterStatus::Continue;
    }
  }

  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    session_id_ = callbacks.sessionId();
  }

private:
  int upstream_bytes_to_drain_;
  bool stop_iteration_on_first_write_{false};
  uint64_t session_id_;
};

class DrainerUdpSessionWriteFilterConfigFactory : public FactoryBase<WriteDrainerConfig> {
public:
  DrainerUdpSessionWriteFilterConfigFactory() : FactoryBase("test.udp_session.drainer_write") {}

private:
  Network::UdpSessionFilterFactoryCb
  createFilterFactoryFromProtoTyped(const WriteDrainerConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addWriteFilter(std::make_unique<DrainerUdpSessionWriteFilter>(
          config.upstream_bytes_to_drain(), config.stop_iteration_on_first_write()));
    };
  }
};

static Registry::RegisterFactory<DrainerUdpSessionWriteFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_drainer_udp_session_write_filter_;

class DrainerUdpSessionFilter : public Filter,
                                public DrainerUdpSessionReadFilter,
                                public DrainerUdpSessionWriteFilter {
public:
  DrainerUdpSessionFilter(int downstream_bytes_to_drain, int upstream_bytes_to_drain,
                          bool stop_iteration_on_new_session, bool stop_iteration_on_first_read,
                          bool continue_filter_chain, bool stop_iteration_on_first_write)
      : DrainerUdpSessionReadFilter(downstream_bytes_to_drain, stop_iteration_on_new_session,
                                    stop_iteration_on_first_read, continue_filter_chain),
        DrainerUdpSessionWriteFilter(upstream_bytes_to_drain, stop_iteration_on_first_write) {}
};

class DrainerUdpSessionFilterConfigFactory : public FactoryBase<DrainerConfig> {
public:
  DrainerUdpSessionFilterConfigFactory() : FactoryBase("test.udp_session.drainer") {}

private:
  Network::UdpSessionFilterFactoryCb
  createFilterFactoryFromProtoTyped(const DrainerConfig& config,
                                    Server::Configuration::FactoryContext&) override {
    return [config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addFilter(std::make_shared<DrainerUdpSessionFilter>(
          config.downstream_bytes_to_drain(), config.upstream_bytes_to_drain(),
          config.stop_iteration_on_new_session(), config.stop_iteration_on_first_read(),
          config.continue_filter_chain(), config.stop_iteration_on_first_write()));
    };
  }
};

static Registry::RegisterFactory<DrainerUdpSessionFilterConfigFactory,
                                 NamedUdpSessionFilterConfigFactory>
    register_drainer_udp_session_filter_;

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
