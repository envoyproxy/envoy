#include "contrib/generic_proxy/filters/network/source/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

REGISTER_FACTORY(ServiceMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(HostMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(PathMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(MethodMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(PropertyMatchDataInputFactory, Matcher::DataInputFactory<Request>);

REGISTER_FACTORY(RequestMatchDataInputFactory, Matcher::DataInputFactory<Request>);

using StringMatcherImpl = Matchers::StringMatcherImpl<StringMatcherProto>;

RequestMatchInputMatcher::RequestMatchInputMatcher(const RequestMatcherProto& proto_config) {

  if (proto_config.has_host()) {
    host_ = std::make_unique<StringMatcherImpl>(proto_config.host());
  }
  if (proto_config.has_path()) {
    path_ = std::make_unique<StringMatcherImpl>(proto_config.path());
  }
  if (proto_config.has_method()) {
    method_ = std::make_unique<StringMatcherImpl>(proto_config.method());
  }

  for (const auto& property : proto_config.properties()) {
    properties_.push_back(
        {property.name(), std::make_unique<StringMatcherImpl>(property.string_match())});
  }
}

bool RequestMatchInputMatcher::match(const Matcher::MatchingDataType& input) {
  if (!absl::holds_alternative<std::shared_ptr<Matcher::CustomMatchData>>(input)) {
    return false;
  }

  const auto* typed_data = dynamic_cast<const RequestMatchData*>(
      absl::get<std::shared_ptr<Matcher::CustomMatchData>>(input).get());

  if (typed_data == nullptr) {
    return false;
  }

  return match(typed_data->request());
}

bool RequestMatchInputMatcher::match(const Request& request) {
  // TODO(wbpcode): may add more debug log for request match?
  if (host_ != nullptr) {
    if (!host_->match(request.host())) {
      // Host does not match.
      return false;
    }
  }

  if (path_ != nullptr) {
    if (!path_->match(request.path())) {
      // Path does not match.
      return false;
    }
  }

  if (method_ != nullptr) {
    if (!method_->match(request.method())) {
      // Method does not match.
      return false;
    }
  }

  for (const auto& property : properties_) {
    if (auto val = request.get(property.first); val.has_value()) {
      if (!property.second->match(val.value())) {
        // Property does not match.
        return false;
      }
    } else {
      // Property does not exist.
      return false;
    }
  }

  // All matchers passed.
  return true;
}

Matcher::InputMatcherFactoryCb RequestMatchDataInputMatcherFactory::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<const RequestMatcherProto&>(
      config, factory_context.messageValidationVisitor());

  return [proto_config]() -> Matcher::InputMatcherPtr {
    return std::make_unique<RequestMatchInputMatcher>(proto_config);
  };
}

REGISTER_FACTORY(RequestMatchDataInputMatcherFactory, Matcher::InputMatcherFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
