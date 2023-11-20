#pragma once

#include "source/extensions/filters/udp/udp_proxy/session_filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

/**
 * Pass through UDP session read filter. Continue at each state within the series of
 * transitions, and pass through the read data.
 */
class PassThroughReadFilter : public virtual ReadFilter {
public:
  ReadFilterStatus onNewSession() override { return ReadFilterStatus::Continue; }

  ReadFilterStatus onData(Network::UdpRecvData&) override { return ReadFilterStatus::Continue; }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  };

protected:
  ReadFilterCallbacks* read_callbacks_{};
};

/**
 * Pass through UDP session write filter. Continue at each state within the series of
 * transitions, and pass through the read data.
 */
class PassThroughWriteFilter : public virtual WriteFilter {
public:
  WriteFilterStatus onWrite(Network::UdpRecvData&) override { return WriteFilterStatus::Continue; }

  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  };

protected:
  WriteFilterCallbacks* write_callbacks_{};
};

// A filter which passes all data through with Continue status.
class PassThroughFilter : public Filter,
                          public PassThroughReadFilter,
                          public PassThroughWriteFilter {};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
