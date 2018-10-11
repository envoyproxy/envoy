#include "extensions/filters/network/find_and_replace/find_and_replace.h"

#include <algorithm>

#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

Config::Config(const Envoy::ProtobufWkt::Struct& proto_config) {

  const Envoy::Protobuf::Map<std::string, Envoy::ProtobufWkt::Value>& fields =
      proto_config.fields();

  auto iterator = fields.find("input_rewrite_from");
  if (iterator != fields.end()) {
    input_rewrite_from_ = iterator->second.string_value();
  }

  iterator = fields.find("input_rewrite_to");
  if (iterator != fields.end()) {
    input_rewrite_to_ = iterator->second.string_value();
  }

  iterator = fields.find("output_rewrite_from");
  if (iterator != fields.end()) {
    output_rewrite_from_ = iterator->second.string_value();
  }

  iterator = fields.find("output_rewrite_to");
  if (iterator != fields.end()) {
    output_rewrite_to_ = iterator->second.string_value();
  }
}

Filter::Filter(const ConfigConstSharedPtr& config)
    : config_(config), is_read_active_(false), is_write_active_(false) {
  if (config_->input_rewrite_from().size() > 0 || config_->input_rewrite_to().size() > 0) {
    ENVOY_LOG(debug,
              "either input_rewrite_from or input_rewrite_to is not empty, enabling read filter");
    is_read_active_ = true;
  }

  if (config_->output_rewrite_from().size() > 0 || config_->output_rewrite_to().size() > 0) {
    ENVOY_LOG(
        debug,
        "either output_rewrite_from or output_rewrite_to is not empty, enabling write filter");
    is_write_active_ = true;
  }
}

void Filter::rewrite(Buffer::Instance& data, const std::string& from, const std::string& to) {
  // we only support rewriting from the beginning of the buffer (== 0)
  if (data.search(from.data(), from.size(), 0) == 0) {
    // remove "from" and then prepend "to"
    data.drain(from.size());
    data.prepend(to);
  }
}

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool) {
  if (is_read_active_) {
    // we require a certain minimum amount data before we try to rewrite
    if (data.length() < config_->input_rewrite_from().size()) {
      return Network::FilterStatus::StopIteration;
    }

    is_read_active_ = false;

    rewrite(data, config_->input_rewrite_from(), config_->input_rewrite_to());
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onWrite(Buffer::Instance& data, bool) {
  if (is_write_active_) {
    // we require a certain minimum amount data before we try to rewrite
    if (data.length() < config_->output_rewrite_from().size()) {
      return Network::FilterStatus::StopIteration;
    }

    is_write_active_ = false;

    rewrite(data, config_->output_rewrite_from(), config_->output_rewrite_to());
  }

  return Network::FilterStatus::Continue;
}

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
