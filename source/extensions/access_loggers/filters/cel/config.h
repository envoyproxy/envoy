#include "envoy/access_log/access_log.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

class CELAccessLogExtensionFilterFactory : public Envoy::AccessLog::ExtensionFilterFactory {
public:
  Envoy::AccessLog::FilterPtr
  createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config, Runtime::Loader&,
               Random::RandomGenerator&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.access_loggers.extension_filters.cel"; }

private:
  Extensions::Filters::Common::Expr::Builder& getOrCreateBuilder();
  Extensions::Filters::Common::Expr::BuilderPtr expr_builder_;
};

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
