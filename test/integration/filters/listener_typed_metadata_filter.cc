#include "envoy/http/filter.h"
#include "envoy/network/listener.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr absl::string_view kFilterName = "listener-typed-metadata-filter";
constexpr absl::string_view kMetadataKey = "test.listener.typed.metadata";
constexpr absl::string_view kExpectedMetadataValue = "hello world";

// A test filter that verifies the typed metadata attached to the listener is stored correctly.
class Baz : public Config::TypedMetadata::Object {
public:
  std::string item_;
};

class BazTypedMetadataFactory : public Network::ListenerTypedMetadataFactory {
public:
  std::string name() const override { return std::string(kMetadataKey); }

  std::unique_ptr<const Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Struct&) const override {
    ADD_FAILURE() << "Filter should not parse struct-typed metadata.";
    return nullptr;
  }
  std::unique_ptr<const Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Any& d) const override {
    ProtobufWkt::StringValue v;
    EXPECT_TRUE(d.UnpackTo(&v));
    auto object = std::make_unique<Baz>();
    object->item_ = v.value();
    return object;
  }
};

class ListenerTypedMetadataFilter : public Http::PassThroughFilter {
public:
  ListenerTypedMetadataFilter() = default;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                       "successfully_handled_request");
    return Http::FilterHeadersStatus::Continue;
  }
};

class ListenerTypedMetadataFilterFactory
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  ListenerTypedMetadataFilterFactory() : EmptyHttpFilterConfig(std::string(kFilterName)) {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext& context) override {

    // Main assertions to ensure the metadata from the listener was parsed correctly.
    const auto& typed_metadata = context.listenerInfo().typedMetadata();
    const Baz* value = typed_metadata.get<Baz>(std::string(kMetadataKey));
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(value->item_, kExpectedMetadataValue);

    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<ListenerTypedMetadataFilter>());
    };
  }
};

REGISTER_FACTORY(BazTypedMetadataFactory, Network::ListenerTypedMetadataFactory);
REGISTER_FACTORY(ListenerTypedMetadataFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace
} // namespace Envoy
