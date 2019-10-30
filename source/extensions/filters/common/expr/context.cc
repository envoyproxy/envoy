#include "extensions/filters/common/expr/context.h"

#include "absl/strings/numbers.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

namespace {

absl::optional<CelValue> convertHeaderEntry(const Http::HeaderEntry* header) {
  if (header == nullptr) {
    return {};
  }
  return CelValue::CreateString(header->value().getStringView());
}

absl::optional<CelValue> extractSslInfo(const Ssl::ConnectionInfo& ssl_info,
                                        absl::string_view value) {
  if (value == TLSVersion) {
    return CelValue::CreateString(&ssl_info.tlsVersion());
  } else if (value == SubjectLocalCertificate) {
    return CelValue::CreateString(&ssl_info.subjectLocalCertificate());
  } else if (value == SubjectPeerCertificate) {
    return CelValue::CreateString(&ssl_info.subjectPeerCertificate());
  } else if (value == URISanLocalCertificate) {
    if (ssl_info.uriSanLocalCertificate().size() > 0) {
      return CelValue::CreateString(&ssl_info.uriSanLocalCertificate()[0]);
    }
  } else if (value == URISanPeerCertificate) {
    if (ssl_info.uriSanPeerCertificate().size() > 0) {
      return CelValue::CreateString(&ssl_info.uriSanPeerCertificate()[0]);
    }
  } else if (value == DNSSanLocalCertificate) {
    if (ssl_info.dnsSansLocalCertificate().size() > 0) {
      return CelValue::CreateString(&ssl_info.dnsSansLocalCertificate()[0]);
    }
  } else if (value == DNSSanPeerCertificate) {
    if (ssl_info.dnsSansPeerCertificate().size() > 0) {
      return CelValue::CreateString(&ssl_info.dnsSansPeerCertificate()[0]);
    }
  }
  return {};
}

} // namespace

absl::optional<CelValue> HeadersWrapper::operator[](CelValue key) const {
  if (value_ == nullptr || !key.IsString()) {
    return {};
  }
  auto out = value_->get(Http::LowerCaseString(std::string(key.StringOrDie().value())));
  return convertHeaderEntry(out);
}

absl::optional<CelValue> RequestWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();

  if (value == Headers) {
    return CelValue::CreateMap(&headers_);
  } else if (value == Time) {
    return CelValue::CreateTimestamp(absl::FromChrono(info_.startTime()));
  } else if (value == Size) {
    // it is important to make a choice whether to rely on content-length vs stream info
    // (which is not available at the time of the request headers)
    if (headers_.value_ != nullptr && headers_.value_->ContentLength() != nullptr) {
      int64_t length;
      if (absl::SimpleAtoi(headers_.value_->ContentLength()->value().getStringView(), &length)) {
        return CelValue::CreateInt64(length);
      }
    } else {
      return CelValue::CreateInt64(info_.bytesReceived());
    }
  } else if (value == Duration) {
    auto duration = info_.requestComplete();
    if (duration.has_value()) {
      return CelValue::CreateDuration(absl::FromChrono(duration.value()));
    }
  }

  if (headers_.value_ != nullptr) {
    if (value == Path) {
      return convertHeaderEntry(headers_.value_->Path());
    } else if (value == UrlPath) {
      absl::string_view path = headers_.value_->Path()->value().getStringView();
      size_t query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        return CelValue::CreateString(path);
      }
      return CelValue::CreateString(path.substr(0, query_offset));
    } else if (value == Host) {
      return convertHeaderEntry(headers_.value_->Host());
    } else if (value == Scheme) {
      return convertHeaderEntry(headers_.value_->Scheme());
    } else if (value == Method) {
      return convertHeaderEntry(headers_.value_->Method());
    } else if (value == Referer) {
      return convertHeaderEntry(headers_.value_->Referer());
    } else if (value == ID) {
      return convertHeaderEntry(headers_.value_->RequestId());
    } else if (value == UserAgent) {
      return convertHeaderEntry(headers_.value_->UserAgent());
    } else if (value == TotalSize) {
      return CelValue::CreateInt64(info_.bytesReceived() + headers_.value_->byteSize().value());
    }
  }
  return {};
}

absl::optional<CelValue> ResponseWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Code) {
    auto code = info_.responseCode();
    if (code.has_value()) {
      return CelValue::CreateInt64(code.value());
    }
  } else if (value == Size) {
    return CelValue::CreateInt64(info_.bytesSent());
  } else if (value == Headers) {
    return CelValue::CreateMap(&headers_);
  } else if (value == Trailers) {
    return CelValue::CreateMap(&trailers_);
  }
  return {};
}

absl::optional<CelValue> ConnectionWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == MTLS) {
    return CelValue::CreateBool(info_.downstreamSslConnection() != nullptr &&
                                info_.downstreamSslConnection()->peerCertificatePresented());
  } else if (value == RequestedServerName) {
    return CelValue::CreateString(&info_.requestedServerName());
  }

  auto ssl_info = info_.downstreamSslConnection();
  if (ssl_info != nullptr) {
    return extractSslInfo(*ssl_info, value);
  }

  return {};
}

absl::optional<CelValue> UpstreamWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Address) {
    auto upstream_host = info_.upstreamHost();
    if (upstream_host != nullptr && upstream_host->address() != nullptr) {
      return CelValue::CreateString(upstream_host->address()->asStringView());
    }
  } else if (value == Port) {
    auto upstream_host = info_.upstreamHost();
    if (upstream_host != nullptr && upstream_host->address() != nullptr &&
        upstream_host->address()->ip() != nullptr) {
      return CelValue::CreateInt64(upstream_host->address()->ip()->port());
    }
  }

  auto ssl_info = info_.upstreamSslConnection();
  if (ssl_info != nullptr) {
    return extractSslInfo(*ssl_info, value);
  }

  return {};
}

absl::optional<CelValue> PeerWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Address) {
    if (local_) {
      return CelValue::CreateString(info_.downstreamLocalAddress()->asStringView());
    } else {
      return CelValue::CreateString(info_.downstreamRemoteAddress()->asStringView());
    }
  } else if (value == Port) {
    if (local_) {
      if (info_.downstreamLocalAddress()->ip() != nullptr) {
        return CelValue::CreateInt64(info_.downstreamLocalAddress()->ip()->port());
      }
    } else {
      if (info_.downstreamRemoteAddress()->ip() != nullptr) {
        return CelValue::CreateInt64(info_.downstreamRemoteAddress()->ip()->port());
      }
    }
  }

  return {};
}

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
