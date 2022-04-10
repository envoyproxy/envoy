#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "test/integration/filters/inspect_data_listener_filter_config.pb.h"
#include "test/integration/filters/inspect_data_listener_filter_config.pb.validate.h"

namespace Envoy {

class InspectDataListenerConfig {
public:
  InspectDataListenerConfig(uint64_t max_read_bytes, bool close_connection)
      : max_read_bytes_(max_read_bytes), close_connection_(close_connection) {}

  uint64_t max_read_bytes_;
  bool close_connection_;
};

class InspectDataListenerFilter : public Network::ListenerFilter {
public:
  InspectDataListenerFilter(std::shared_ptr<InspectDataListenerConfig> config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    FANCY_LOG(debug, "in InspectDataListenerFilter::onAccept");
    cb_ = &cb;
    return Network::FilterStatus::StopIteration;
  }

  size_t maxReadBytes() const override { return config_->max_read_bytes_; }

  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override {
    FANCY_LOG(debug, "in InspectDataListenerFilter::onData");
    if (buffer.rawSlice().len_ >= config_->max_read_bytes_) {
      if (config_->close_connection_) {
        FANCY_LOG(debug,
                  "in InspectDataListenerFilter::onData, close socket and stop the iteration.");
        cb_->socket().ioHandle().close();
        return Network::FilterStatus::StopIteration;
      } else {
        FANCY_LOG(
            debug,
            "in InspectDataListenerFilter::onData, get enough data and continue the iteration.");
        return Network::FilterStatus::Continue;
      }
    } else {
      FANCY_LOG(
          debug,
          "in InspectDataListenerFilter::onData, waiting for more data and stop the iteration.");
      return Network::FilterStatus::StopIteration;
    }
  }

  Network::ListenerFilterCallbacks* cb_{};
  std::shared_ptr<InspectDataListenerConfig> config_;
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
    auto config = std::make_shared<InspectDataListenerConfig>(proto_config.max_read_bytes(),
                                                              proto_config.close_connection());
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
