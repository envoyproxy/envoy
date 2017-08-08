#pragma once

namespace Envoy {
namespace Network {

/**
 * Implementation of Network::ReadFilter that discards read callbacks.
 */
class ReadFilterBaseImpl : public ReadFilter {
public:
  void initializeReadFilterCallbacks(ReadFilterCallbacks&) override {}
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
};

/**
 * Implementation of Network::Filter that discards read callbacks.
 */
class FilterBaseImpl : public Filter {
public:
  void initializeReadFilterCallbacks(ReadFilterCallbacks&) override {}
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
};

} // namespace Network
} // namespace Envoy
