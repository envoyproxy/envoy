#pragma once

#include <algorithm>

#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

class Config {
public:
  Config(const Envoy::ProtobufWkt::Struct& proto_config);

  const std::string& input_rewrite_from() const { return input_rewrite_from_; };
  const std::string& input_rewrite_to() const { return input_rewrite_to_; };
  const std::string& output_rewrite_from() const { return output_rewrite_from_; };
  const std::string& output_rewrite_to() const { return output_rewrite_to_; };

private:
  std::string input_rewrite_from_;
  std::string input_rewrite_to_;
  std::string output_rewrite_from_;
  std::string output_rewrite_to_;
};

typedef std::shared_ptr<const Config> ConfigConstSharedPtr;

/**
 * Implementation of the FindAndReplace filter. This filter allows a replacement of the first
 * bytes of a connection.
 */
class Filter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigConstSharedPtr& config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

private:
  void rewrite(Buffer::Instance& data, const std::string& from, const std::string& to);

  const ConfigConstSharedPtr config_;
  bool is_read_active_;
  bool is_write_active_;
};

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
