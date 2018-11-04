#include "extensions/filters/network/find_and_replace/find_and_replace.h"

#include <algorithm>

#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

Config::Config(
    const envoy::config::filter::network::find_and_replace::v2alpha1::FindAndReplace& config)
    : input_rewrite_from_(config.input_rewrite_from()),
      input_rewrite_to_(config.input_rewrite_to()),
      output_rewrite_from_(config.output_rewrite_from()),
      output_rewrite_to_(config.output_rewrite_to()) {}

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
