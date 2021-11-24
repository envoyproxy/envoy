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
  CustomVocabularyWrapper(Protobuf::Arena& arena,
                          const StreamInfo::StreamInfo& info,
                          const Http::RequestHeaderMap* request_headers,
                          const Http::ResponseHeaderMap* response_headers,
                          const Http::ResponseTrailerMap* response_trailers)
      : arena_(arena), info_(info) {
    request_headers_ = request_headers;
    response_headers_ = response_headers;
    response_trailers_ = response_trailers;
  }
  absl::optional<CelValue> operator[](CelValue key) const override;
  const Http::RequestHeaderMap* request_headers() { return request_headers_; }
  const Http::ResponseHeaderMap* response_headers() { return response_headers_; }
  const Http::ResponseTrailerMap* response_trailers() { return response_trailers_; }

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
