#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

std::string PreserveCaseHeaderFormatter::format(absl::string_view key) const {
  const auto remembered_key_itr = original_header_keys_.find(key);
  // TODO(mattklein123): We can avoid string copies here if the formatter interface allowed us
  // to return something like GetAllOfHeaderAsStringResult with both a string_view and an
  // optional backing string. We can do this in a follow up if there is interest.
  // TODO(mattklein123): This implementation does not cover headers added by Envoy that may need
  // do be in a different case. We can handle this in the future by extending this formatter to
  // have an "inner formatter" that would allow performing proper case (for example) on unknown
  // headers.
  if (remembered_key_itr != original_header_keys_.end()) {
    return *remembered_key_itr;
  } else {
    return std::string(key);
  }
}

void PreserveCaseHeaderFormatter::processKey(absl::string_view key) {
  // Note: This implementation will only remember the first instance of a particular header key.
  // So for example "Foo" followed by "foo" will both be serialized as "Foo" on the way out. We
  // could do better here but it's unlikely it's worth it and we can see if anyone complains about
  // the implementation.
  original_header_keys_.emplace(key);
}

class PreserveCaseFormatterFactory : public Envoy::Http::StatefulHeaderKeyFormatterFactory {
public:
  // Envoy::Http::StatefulHeaderKeyFormatterFactory
  Envoy::Http::StatefulHeaderKeyFormatterPtr create() override {
    return std::make_unique<PreserveCaseHeaderFormatter>();
  }
};

class PreserveCaseFormatterFactoryConfig
    : public Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig {
public:
  // Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig
  std::string name() const override { return "preserve_case"; }
  Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr
  createFromProto(const Protobuf::Message&) override {
    return std::make_shared<PreserveCaseFormatterFactory>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::header_formatters::preserve_case::v3::
                                PreserveCaseFormatterConfig>();
  }
};

REGISTER_FACTORY(PreserveCaseFormatterFactoryConfig,
                 Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig);

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
