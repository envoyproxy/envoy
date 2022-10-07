#include "source/extensions/common/aws/utility.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "curl/curl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr absl::string_view PATH_SPLITTER = "/";
constexpr absl::string_view QUERY_PARAM_SEPERATOR = "=";
constexpr absl::string_view QUERY_SEPERATOR = "&";
constexpr absl::string_view QUERY_SPLITTER = "?";
constexpr absl::string_view RESERVED_CHARS = "-._~";
constexpr absl::string_view S3_SERVICE_NAME = "s3";
const std::string URI_ENCODE = "%{:02X}";
const std::string URI_DOUBLE_ENCODE = "%25{:02X}";

std::map<std::string, std::string>
Utility::canonicalizeHeaders(const Http::RequestHeaderMap& headers,
                             const std::vector<Matchers::StringMatcherPtr>& excluded_headers) {
  std::map<std::string, std::string> out;
  headers.iterate(
      [&out, &excluded_headers](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
        // Skip empty headers
        if (entry.key().empty() || entry.value().empty()) {
          return Http::HeaderMap::Iterate::Continue;
        }
        // Pseudo-headers should not be canonicalized
        if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        const auto key = entry.key().getStringView();
        if (std::any_of(excluded_headers.begin(), excluded_headers.end(),
                        [&key](const Matchers::StringMatcherPtr& matcher) {
                          return matcher->match(key);
                        })) {
          return Http::HeaderMap::Iterate::Continue;
        }

        std::string value(entry.value().getStringView());
        // Remove leading, trailing, and deduplicate repeated ascii spaces
        absl::RemoveExtraAsciiWhitespace(&value);
        const auto iter = out.find(std::string(entry.key().getStringView()));
        // If the entry already exists, append the new value to the end
        if (iter != out.end()) {
          iter->second += fmt::format(",{}", value);
        } else {
          out.emplace(std::string(entry.key().getStringView()), value);
        }
        return Http::HeaderMap::Iterate::Continue;
      });
  // The AWS SDK has a quirk where it removes "default ports" (80, 443) from the host headers
  // Additionally, we canonicalize the :authority header as "host"
  // TODO(lavignes): This may need to be tweaked to canonicalize :authority for HTTP/2 requests
  const absl::string_view authority_header = headers.getHostValue();
  if (!authority_header.empty()) {
    const auto parts = StringUtil::splitToken(authority_header, ":");
    if (parts.size() > 1 && (parts[1] == "80" || parts[1] == "443")) {
      // Has default port, so use only the host part
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(parts[0]));
    } else {
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(authority_header));
    }
  }
  return out;
}

std::string Utility::createCanonicalRequest(
    absl::string_view service_name, absl::string_view method, absl::string_view path,
    const std::map<std::string, std::string>& canonical_headers, absl::string_view content_hash) {
  std::vector<absl::string_view> parts;
  parts.emplace_back(method);
  // don't include the query part of the path
  const auto path_part = StringUtil::cropRight(path, QUERY_SPLITTER);
  const auto canonicalized_path = path_part.empty()
                                      ? std::string{PATH_SPLITTER}
                                      : canonicalizePathString(path_part, service_name);
  parts.emplace_back(canonicalized_path);
  const auto query_part = StringUtil::cropLeft(path, QUERY_SPLITTER);
  // if query_part == path_part, then there is no query
  const auto canonicalized_query =
      query_part == path_part ? EMPTY_STRING : Utility::canonicalizeQueryString(query_part);
  parts.emplace_back(absl::string_view(canonicalized_query));
  std::vector<std::string> formatted_headers;
  formatted_headers.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    formatted_headers.emplace_back(fmt::format("{}:{}", header.first, header.second));
    parts.emplace_back(formatted_headers.back());
  }
  // need an extra blank space after the canonical headers
  parts.emplace_back(EMPTY_STRING);
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  parts.emplace_back(signed_headers);
  parts.emplace_back(content_hash);
  return absl::StrJoin(parts, "\n");
}

/**
 * Normalizes the path string based on AWS requirements.
 * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
 */
std::string Utility::canonicalizePathString(absl::string_view path_string,
                                            absl::string_view service_name) {
  // If service is S3, do not normalize but only encode the path
  if (absl::EqualsIgnoreCase(service_name, S3_SERVICE_NAME)) {
    return encodePathSegment(path_string, service_name);
  }
  // If service is not S3, normalize and encode the path
  const auto path_segments = StringUtil::splitToken(path_string, std::string{PATH_SPLITTER});
  std::vector<std::string> path_list;
  path_list.reserve(path_segments.size());
  for (const auto& path_segment : path_segments) {
    if (path_segment.empty()) {
      continue;
    }
    path_list.emplace_back(encodePathSegment(path_segment, service_name));
  }
  auto canonical_path_string =
      fmt::format("{}{}", PATH_SPLITTER, absl::StrJoin(path_list, PATH_SPLITTER));
  // Handle corner case when path ends with '/'
  if (absl::EndsWith(path_string, PATH_SPLITTER) && canonical_path_string.size() > 1) {
    canonical_path_string.push_back(PATH_SPLITTER[0]);
  }
  return canonical_path_string;
}

bool isReservedChar(const char c) {
  return std::isalnum(c) || RESERVED_CHARS.find(c) != std::string::npos;
}

void encodeS3Path(std::string& encoded, const char& c) {
  // Do not encode '/' for S3
  if (c == PATH_SPLITTER[0]) {
    encoded.push_back(c);
  } else {
    absl::StrAppend(&encoded, fmt::format(URI_ENCODE, c));
  }
}

std::string Utility::encodePathSegment(absl::string_view decoded, absl::string_view service_name) {
  std::string encoded;
  for (char c : decoded) {
    if (isReservedChar(c)) {
      // Escape unreserved chars from RFC 3986
      encoded.push_back(c);
    } else if (absl::EqualsIgnoreCase(service_name, S3_SERVICE_NAME)) {
      encodeS3Path(encoded, c);
    } else {
      // TODO: @aws, There is some inconsistency between AWS services if this should be double
      // encoded or not. We need to parameterize this and expose this in the config. Ref:
      // https://github.com/aws/aws-sdk-cpp/blob/main/aws-cpp-sdk-core/source/auth/AWSAuthSigner.cpp#L79-L93
      absl::StrAppend(&encoded, fmt::format(URI_ENCODE, c));
    }
  }
  return encoded;
}

/**
 * Normalizes the query string based on AWS requirements.
 * See step 3 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
 */
std::string Utility::canonicalizeQueryString(absl::string_view query_string) {
  // Sort query string based on param name and append "=" if value is missing
  const auto query_fragments = StringUtil::splitToken(query_string, QUERY_SEPERATOR);
  std::vector<std::pair<std::string, std::string>> query_list;
  for (const auto& query_fragment : query_fragments) {
    // Only split at the first "=" and encode the rest
    const std::vector<std::string> query =
        absl::StrSplit(query_fragment, absl::MaxSplits(QUERY_PARAM_SEPERATOR, 1));
    if (!query.empty()) {
      const absl::string_view param = query[0];
      const absl::string_view value = query.size() > 1 ? query[1] : EMPTY_STRING;
      query_list.emplace_back(std::make_pair(param, value));
    }
  }
  // Sort query params by name and value
  std::sort(query_list.begin(), query_list.end());
  // Encode query params name and value separately
  for (auto& query : query_list) {
    query = std::make_pair(Utility::encodeQueryParam(query.first),
                           Utility::encodeQueryParam(query.second));
  }
  return absl::StrJoin(query_list, QUERY_SEPERATOR, absl::PairFormatter(QUERY_PARAM_SEPERATOR));
}

std::string Utility::encodeQueryParam(absl::string_view decoded) {
  std::string encoded;
  for (char c : decoded) {
    if (isReservedChar(c) || c == '%') {
      // Escape unreserved chars from RFC 3986
      encoded.push_back(c);
    } else if (c == '+') {
      // Encode '+' as space
      absl::StrAppend(&encoded, "%20");
    } else if (c == QUERY_PARAM_SEPERATOR[0]) {
      // Double encode '='
      absl::StrAppend(&encoded, fmt::format(URI_DOUBLE_ENCODE, c));
    } else {
      absl::StrAppend(&encoded, fmt::format(URI_ENCODE, c));
    }
  }
  return encoded;
}

std::string
Utility::joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers) {
  return absl::StrJoin(canonical_headers, ";", [](auto* out, const auto& pair) {
    return absl::StrAppend(out, pair.first);
  });
}

static size_t curlCallback(char* ptr, size_t, size_t nmemb, void* data) {
  auto buf = static_cast<std::string*>(data);
  buf->append(ptr, nmemb);
  return nmemb;
}

absl::optional<std::string> Utility::metadataFetcher(const std::string& host,
                                                     const std::string& path,
                                                     const std::string& auth_token) {
  static const size_t MAX_RETRIES = 4;
  static const std::chrono::milliseconds RETRY_DELAY{1000};
  static const std::chrono::seconds TIMEOUT{5};

  CURL* const curl = curl_easy_init();
  if (!curl) {
    return absl::nullopt;
  };

  const std::string url = fmt::format("http://{}/{}", host, path);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT.count());
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  std::string buffer;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

  struct curl_slist* headers = nullptr;
  if (!auth_token.empty()) {
    const std::string auth = fmt::format("Authorization: {}", auth_token);
    headers = curl_slist_append(headers, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  for (size_t retry = 0; retry < MAX_RETRIES; retry++) {
    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      break;
    }
    ENVOY_LOG_MISC(debug, "Could not fetch AWS metadata: {}", curl_easy_strerror(res));
    buffer.clear();
    std::this_thread::sleep_for(RETRY_DELAY);
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return buffer.empty() ? absl::nullopt : absl::optional<std::string>(buffer);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
