#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

class CustomVocabularyWrapper : public BaseWrapper {
public:
  CustomVocabularyWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : arena_(arena), info_(info) {}
  absl::optional<CelValue> operator[](CelValue key) const override;

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
};

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
