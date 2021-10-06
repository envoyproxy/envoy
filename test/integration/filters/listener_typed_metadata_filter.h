#include "envoy/http/filter.h"
#include "envoy/network/listener.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

namespace {
inline constexpr absl::string_view kMetadataKey = "test.listener.typed.metadata";
}

// A test filter that verifies the typed metadata attached to the listener is stored correctly.
class Baz : public Config::TypedMetadata::Object {
public:
  std::string item_;
};

class BazTypedMetadataFactory : public Network::ListenerTypedMetadataFactory {
public:
  std::string name() const override { return kMetadataKey; }

  std::unique_ptr<const Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Struct& d) const override {
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
  ListenerTypedMetadataFilter() {}
};

class ListenerTypedMetadataFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<ProtobufWkt::Empty> {
public:
  ListenerTypedMetadataFilterFactory() : FactoryBase("set-response-code-filter") {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtobufWkt::Empty&, const std::string&,
                                    Server::Configuration::FactoryContext& context) override {

    const auto& typed_metadata = context.listenerTypedMetadata();
    const Baz* value = typed_metadata.get(kMetadataKey);
    EXPECT_NEQ(value, nullptr);
    EXPECT_FALSE(value->item.empty());

    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<ListenerTypedMetadataFilter>());
    };
  }
};

REGISTER_FACTORY(BazTypedMetadataFactory, Network::ListenerTypedMetadataFactory);
REGISTER_FACTORY(ListenerTypedMetadataFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
