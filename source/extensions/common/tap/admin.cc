#include "extensions/common/tap/admin.h"

#include "envoy/admin/v2alpha/tap.pb.h"
#include "envoy/admin/v2alpha/tap.pb.validate.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(tap_admin_handler);

AdminHandlerSharedPtr AdminHandler::getSingleton(Server::Admin& admin,
                                                 Singleton::Manager& singleton_manager,
                                                 Event::Dispatcher& main_thread_dispatcher) {
  return singleton_manager.getTyped<AdminHandler>(
      SINGLETON_MANAGER_REGISTERED_NAME(tap_admin_handler), [&admin, &main_thread_dispatcher] {
        return std::make_shared<AdminHandler>(admin, main_thread_dispatcher);
      });
}

AdminHandler::AdminHandler(Server::Admin& admin, Event::Dispatcher& main_thread_dispatcher)
    : admin_(admin), main_thread_dispatcher_(main_thread_dispatcher) {
  bool rc =
      admin_.addHandler("/tap", "tap filter control", MAKE_ADMIN_HANDLER(handler), true, true);
  RELEASE_ASSERT(rc, "/tap admin endpoint is taken");
}

AdminHandler::~AdminHandler() {
  bool rc = admin_.removeHandler("/tap");
  ASSERT(rc);
}

Http::Code AdminHandler::handler(absl::string_view, Http::HeaderMap&, Buffer::Instance& response,
                                 Server::AdminStream& admin_stream) {
  if (attached_request_.has_value()) {
    response.add("An attached /tap admin stream already exists. Detach it.");
    return Http::Code::BadRequest;
  }

  if (admin_stream.getRequestBody() == nullptr) {
    response.add("/tap requires a JSON/YAML body");
    return Http::Code::BadRequest;
  }

  envoy::admin::v2alpha::TapRequest tap_request;
  try {
    MessageUtil::loadFromYamlAndValidate(admin_stream.getRequestBody()->toString(), tap_request);
  } catch (EnvoyException& e) {
    response.add(e.what());
    return Http::Code::BadRequest;
  }

  ENVOY_LOG(debug, "tap admin request for config_id={}", tap_request.config_id());
  if (config_id_map_.count(tap_request.config_id()) == 0) {
    response.add(fmt::format("Unknown config id '{}'. No extension has registered with this id.",
                             tap_request.config_id()));
    return Http::Code::BadRequest;
  }
  for (auto config : config_id_map_[tap_request.config_id()]) {
    config->newTapConfig(std::move(*tap_request.mutable_tap_config()), this);
  }

  admin_stream.setEndStreamOnComplete(false);
  admin_stream.addOnDestroyCallback([this] {
    for (auto config : config_id_map_[attached_request_.value().config_id_]) {
      ENVOY_LOG(debug, "detach tap admin request for config_id={}",
                attached_request_.value().config_id_);
      config->clearTapConfig();
      attached_request_ = absl::nullopt;
    }
  });
  attached_request_.emplace(tap_request.config_id(), &admin_stream);
  return Http::Code::OK;
}

void AdminHandler::registerConfig(ExtensionConfig& config, const std::string& config_id) {
  ASSERT(!config_id.empty());
  ASSERT(config_id_map_[config_id].count(&config) == 0);
  config_id_map_[config_id].insert(&config);
}

void AdminHandler::unregisterConfig(ExtensionConfig& config) {
  ASSERT(!config.adminId().empty());
  ASSERT(config_id_map_[config.adminId()].count(&config) == 1);
  config_id_map_[config.adminId()].erase(&config);
  if (config_id_map_[config.adminId()].size() == 0) {
    config_id_map_.erase(config.adminId());
  }
}

void AdminHandler::submitBufferedTrace(std::shared_ptr<Protobuf::Message> trace) {
  // TODO(mattklein123): The only reason this takes a shared_ptr is because currently a posted
  // lambda cannot take a unique_ptr because we copy the lambda. If we allow posts to happen with
  // a moved lambda we can fix this. I will fix this in a follow up.
  ENVOY_LOG(debug, "admin submitting buffered trace to main thread");
  main_thread_dispatcher_.post([this, trace]() {
    if (attached_request_.has_value()) {
      ENVOY_LOG(debug, "admin writing buffered trace to response");
      Buffer::OwnedImpl json_trace{MessageUtil::getJsonStringFromMessage(*trace, true, true)};
      attached_request_.value().admin_stream_->getDecoderFilterCallbacks().encodeData(json_trace,
                                                                                      false);
    }
  });
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
