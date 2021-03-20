#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.validate.h"
#include "envoy/http/header_formatter.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

class PreserveCaseHeaderFormatter : public Envoy::Http::StatefulHeaderKeyFormatter {
public:
  // Envoy::Http::StatefulHeaderKeyFormatter
  std::string format(absl::string_view key) const override {
    const auto remembered_key = original_header_keys_.find(key);
    if (remembered_key != original_header_keys_.end()) {
      return *remembered_key;
    } else {
      return std::string(key);
    }
  }
  void rememberOriginalHeaderKey(absl::string_view key) override {
    original_header_keys_.emplace(key);
  }

private:
  StringUtil::CaseUnorderedSet original_header_keys_;
};

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
  Envoy::Http::StatefulHeaderKeyFormatterFactoryPtr
  createFromProto(const Protobuf::Message&) override {
    return std::make_unique<PreserveCaseFormatterFactory>();
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