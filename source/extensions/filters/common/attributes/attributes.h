#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/network/filter.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/http/header_map_impl.h"

#include "source/extensions/filters/common/attributes/attributes.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {
using google::api::expr::v1alpha1::MapValue;
using google::api::expr::v1alpha1::Value;
using google::protobuf::Any;

class ExprValueUtil {
public:
  static Value optionalStringValue(const absl::optional<std::string>& str);
  static Value stringValue(const std::string& str);
  static Value int64Value(int64_t n);
  static Value uint64Value(uint64_t n);
  static Value doubleValue(double n);
  static Value boolValue(bool b);
  static Value objectValue(Any o);
  static Value mapValue(MapValue* m);
  static const Value nullValue();
};

class AttrUtils {
public:
  std::vector<std::tuple<absl::string_view, absl::string_view>>
  tokenizeAttrs(const google::protobuf::RepeatedPtrField<std::string> attrs);
  static std::tuple<absl::string_view, absl::string_view> tokenizeAttrPath(absl::string_view path);
}

class AttrState : public Logger::Loggable<Logger::Id::filter> {
public:
  AttrState(StreamInfo::StreamInfo& stream_info,
            std::vector<std::tuple<absl::string_view, absl::string_view>>& specified)
      : stream_info_(info), specified_(specified){};
  void populateRequestAttributes(Envoy::Http::RequestHeaderMap& headers,
                                 ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attrs);
  void populateResponseAttributes(Envoy::Http::ResponseHeaderMap& headers,
                                  ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attrs);

private:
  absl::optional<Value> findValue(absl::string_view root_tok, absl::string_view sub_tok);
  absl::optional<Value> requestSet(absl::string_view name);
  absl::optional<Value> responseSet(absl::string_view path);
  absl::optional<Value> connectionSet(absl::string_view path);
  absl::optional<Value> upstreamSet(absl::string_view path);
  absl::optional<Value> sourceSet(absl::string_view path);
  absl::optional<Value> destinationSet(absl::string_view path);
  absl::optional<Value> metadataSet();
  absl::optional<Value> filterStateSet();

  ProtobufWkt::Map<std::string, ProtobufWkt::Value>& getOrInsert(std::string key);
  std::string getTs();
  std::string formatDuration(absl::Duration duration);
  absl::optional<Value> getGrpcStatus();

  StreamInfo::StreamInfo& stream_info_;
  std::vector<std::tuple<absl::string_view, absl::string_view>> specified_;
};

// using google::api::expr::v1alpha1::Value;

// class RequestPartBroker {
// public:
//   RequestPartBroker(const Http::RequestHeaderMap* headers,
//                     const StreamInfo::StreamInfo& stream_info)
//       : headers_(headers), stream_info_(stream_info) {}
//   absl::optional<Value> value() const {
//     google::api::expr::v1alpha1::MapValue m;
//     for (const absl::string_view s: request_tokens) {
//       MapValue_Entry* e = m.add_entries();
//       e->set_allocated_key(&ExprValueUtil::stringV)

//     }
//     ExprValueUtil::mapValue(m)
//   }
//   absl::optional<Value> operator[](absl::string_view k) const {
//     auto part_tok = request_tokens.find(k);
//     if (part_token == request_tokens.end()) {
//       return absl::nullopt;
//     }

//     int end;
//     switch (part_token->second) {
//     case RequestToken::PATH:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getPathValue()));
//       }
//       break;
//     case RequestToken::URL_PATH:
//       if (headers_ != nullptr && headers_->Path() != nullptr &&
//           headers_->Path()->value() != nullptr) {
//         end = std::max(path.find('\0'), path.find('?'));
//         return ExprValueUtil::stringValue(
//             std::string(headers_->Path()->value().getStringView().substr(0, end)));
//       }
//       break;
//     case RequestToken::HOST:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getHostValue()));
//       }
//       break;
//     case RequestToken::SCHEME:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getSchemeValue()));
//       }
//       break;
//     case RequestToken::METHOD:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getMethodValue()));
//       }
//       break;
//     case RequestToken::HEADERS:
//       ENVOY_LOG(debug, "ignoring unimplemented attribute request.headers");
//       break;
//     case RequestToken::REFERER:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(
//             std::string(headers_->getInlineValue(referer_handle.handle())));
//       }
//       break;
//     case RequestToken::USERAGENT:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getUserAgentValue()));
//       }
//       break;
//     case RequestToken::TIME:
//       return ExprValueUtil::stringValue(getTs());
//       break;
//     case RequestToken::ID:
//       if (headers_ != nullptr) {
//         return ExprValueUtil::stringValue(std::string(headers_->getRequestIdValue()));
//       }
//       break;
//     case RequestToken::PROTOCOL:
//       return ExprValueUtil::optionalStringValue(
//           HttpProtocolStrings[static_cast<int>(stream_info_.protocol().value())]);
//     case RequestToken::DURATION:
//       if (stream_info_.requestComplete().has_value()) {
//         return ExprValueUtil::stringValue(
//             formatDuration(absl::FromChrono(stream_info_.requestComplete().value())));
//       }
//       break;
//     case RequestToken::SIZE:
//       if (headers_ != nullptr && headers_->ContentLength() != nullptr) {
//         int64_t length;
//         if (absl::SimpleAtoi(headers_->ContentLength()->value().getStringView(), &length)) {
//           return ExprValueUtil::uint64Value(length);
//         }
//       } else {
//         return ExprValueUtil::uint64Value(stream_info_.bytesReceived());
//       }
//       break;
//     case RequestToken::TOTAL_SIZE:
//       return ExprValueUtil::uint64Value(
//           stream_info_.bytesReceived() + headers_ != nullptr ? headers_->byteSize() : 0);
//     }
//     return absl::nullopt;
//   }

// private:
//   const Http::RequestHeaderMap* headers_;
//   const StreamInfo::StreamInfo& stream_info_;
// }

// class AttributesBroker {
// public:
//   AttributesBroker(const StreamInfo::StreamInfo& stream_info) : stream_info_(stream_info) {}
//   template <typename InputIterator>
//   absl::optional<Value> getAttribute(InputIterator begin, InputIterator end) {
//     if (begin == end) {
//       return absl::nullopt;
//     }
//     if std
//       ::distance(begin, end) == 1 Value value = getTopAttribute(*begin);

//     while (begin != end) {
//       absl::string_view part = *begin;
//     }
//   }
//   void setRequestHeaders(Http::RequestHeaderMap* headers) { request_headers_ = headers; }
//   void setResponseHeaders(Http::ResponseHeaderMap* headers) { response_headers_ = headers; }

// private:
//   const StreamInfo::StreamInfo& stream_info_;
//   Http::RequestHeaderMap* request_headers_{};
//   Http::ResponseHeaderMap* response_headers_{};
// }

// class AttrBroker {
// public:
//   AttrBroker(const StreamInfo::StreamInfo& info);

// private:
//   setRequestHeaders

// }

// class RequestAttrBroker {
// public:
//   RequestWrapper(Protobuf::Arena& arena, const Http::RequestHeaderMap* headers,
//                  const StreamInfo::StreamInfo& info)
//       : headers_(arena, headers), info_(info) {}
//   absl::optional<Value> operator[](absl::string_view) const override;

// private:
//   const HeadersWrapper<Http::RequestHeaderMap> headers_;
//   const StreamInfo::StreamInfo& info_;
// };
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy