#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "test/integration/filters/inspect_data_listener_filter_config.pb.h"
#include "test/integration/filters/inspect_data_listener_filter_config.pb.validate.h"

namespace Envoy {

class InspectDataListenerConfig {
public:
  InspectDataListenerConfig(uint64_t max_read_bytes, bool close_connection, bool drain,
                            uint64_t new_max_read_bytes)
      : max_read_bytes_(max_read_bytes), close_connection_(close_connection), drain_(drain),
        new_max_read_bytes_(new_max_read_bytes) {}

  uint64_t max_read_bytes_;
  bool close_connection_;
  bool drain_;
  uint64_t new_max_read_bytes_;
};

class InspectDataListenerFilter : public Network::ListenerFilter {
public:
  InspectDataListenerFilter(std::shared_ptr<InspectDataListenerConfig> config)
      : config_(config), max_read_bytes_(config->max_read_bytes_) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    ENVOY_LOG_MISC(debug, "in InspectDataListenerFilter::onAccept");
    cb_ = &cb;
    if (max_read_bytes_ != 0) {
      ENVOY_LOG_MISC(debug, "in InspectDataListenerFilter::onAccept, expect more data");
      return Network::FilterStatus::StopIteration;
    }
    if (config_->close_connection_) {
      ENVOY_LOG_MISC(
          debug, "in InspectDataListenerFilter::onAccept, close socket and stop the iteration.");
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    }
    ENVOY_LOG_MISC(debug, "in InspectDataListenerFilter::onAccept, continue");
    return Network::FilterStatus::Continue;
  }

  size_t maxReadBytes() const override { return max_read_bytes_; }

  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override {
    ENVOY_LOG_MISC(debug, "in InspectDataListenerFilter::onData");
    if (buffer.rawSlice().len_ >= max_read_bytes_) {
      if (config_->close_connection_) {
        ENVOY_LOG_MISC(
            debug, "in InspectDataListenerFilter::onData, close socket and stop the iteration.");
        cb_->socket().ioHandle().close();
        return Network::FilterStatus::StopIteration;
      } else {
        if (config_->drain_) {
          ENVOY_LOG_MISC(debug, "in InspectDataListenerFilter::onData, drain the {} bytes data.",
                         max_read_bytes_);
          buffer.drain(max_read_bytes_);
        }
        if (max_read_bytes_ < config_->new_max_read_bytes_) {
          max_read_bytes_ = config_->new_max_read_bytes_;
          ENVOY_LOG_MISC(debug,
                         "in InspectDataListenerFilter::onData, increase max_read_bytes to {} and "
                         "wating for more data.",
                         max_read_bytes_);
          return Network::FilterStatus::StopIteration;
        }
        ENVOY_LOG_MISC(
            debug,
            "in InspectDataListenerFilter::onData, get enough data and continue the iteration.");
        return Network::FilterStatus::Continue;
      }
    } else {
      ENVOY_LOG_MISC(
          debug,
          "in InspectDataListenerFilter::onData, waiting for more data and stop the iteration.");
      return Network::FilterStatus::StopIteration;
    }
  }

  Network::ListenerFilterCallbacks* cb_{};
  std::shared_ptr<InspectDataListenerConfig> config_;
  uint64_t max_read_bytes_;
};

class InspectDataListenerFilterConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const test::integration::filters::InspectDataListenerFilterConfig&>(
        message, context.messageValidationVisitor());
    auto config = std::make_shared<InspectDataListenerConfig>(
        proto_config.max_read_bytes(), proto_config.close_connection(), proto_config.drain(),
        proto_config.new_max_read_bytes());
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher,
                                         std::make_unique<InspectDataListenerFilter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::InspectDataListenerFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.listener.inspect_data"; }
};

static Registry::RegisterFactory<InspectDataListenerFilterConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    register_;
} // namespace Envoy
