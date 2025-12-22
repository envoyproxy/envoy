#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/utility.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {
namespace Matching {

/**
 * Implementation of HttpMatchingData, providing HTTP specific data to
 * the match tree.
 */
class HttpMatchingDataImpl : public HttpMatchingData {
public:
  explicit HttpMatchingDataImpl(const StreamInfo::StreamInfo& stream_info)
      : stream_info_(stream_info) {}

  static absl::string_view name() { return "http"; }

  void onRequestHeaders(const RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
  }

  void onRequestTrailers(const RequestTrailerMap& request_trailers) {
    request_trailers_ = &request_trailers;
  }

  void onResponseHeaders(const ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
  }

  void onResponseTrailers(const ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
  }

  RequestHeaderMapOptConstRef requestHeaders() const override {
    return makeOptRefFromPtr(request_headers_);
  }

  RequestTrailerMapOptConstRef requestTrailers() const override {
    return makeOptRefFromPtr(request_trailers_);
  }

  ResponseHeaderMapOptConstRef responseHeaders() const override {
    return makeOptRefFromPtr(response_headers_);
  }

  ResponseTrailerMapOptConstRef responseTrailers() const override {
    return makeOptRefFromPtr(response_trailers_);
  }

  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }

  const Network::ConnectionInfoProvider& connectionInfoProvider() const override {
    return stream_info_.downstreamAddressProvider();
  }

private:
  const StreamInfo::StreamInfo& stream_info_;
  const RequestHeaderMap* request_headers_{};
  const ResponseHeaderMap* response_headers_{};
  const RequestTrailerMap* request_trailers_{};
  const ResponseTrailerMap* response_trailers_{};
};

using HttpMatchingDataImplSharedPtr = std::shared_ptr<HttpMatchingDataImpl>;

// A map of named filter chains that have been pre-compiled at HCM configuration time.
// Each entry maps a filter chain name to a list of filter factory callbacks.
// These can be referenced from composite filter actions via filter_chain_ref.
using NamedFilterChainFactoryMap =
    absl::flat_hash_map<std::string, std::vector<Http::FilterFactoryCb>>;
using NamedFilterChainFactoryMapConstSharedPtr = std::shared_ptr<const NamedFilterChainFactoryMap>;

// Thread-local scope for passing named filter chains during HCM config processing.
// This allows the match_delegate factory to access named filter chains defined
// at the HCM level when creating the HttpFilterActionContext.
class NamedFilterChainScope {
public:
  // Sets the current named filter chains for this thread.
  static void set(NamedFilterChainFactoryMapConstSharedPtr chains) { current() = chains; }

  // Gets the current named filter chains for this thread.
  static NamedFilterChainFactoryMapConstSharedPtr get() { return current(); }

  // Clears the current named filter chains for this thread.
  static void clear() { current() = nullptr; }

private:
  static NamedFilterChainFactoryMapConstSharedPtr& current() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(NamedFilterChainFactoryMapConstSharedPtr);
  }
};

// Helper method to set named filter chains for the duration of a scope. This is used
// during HCM config processing to make named chains available to filter factories.
class ScopedNamedFilterChainSetter {
public:
  explicit ScopedNamedFilterChainSetter(NamedFilterChainFactoryMapConstSharedPtr chains) {
    NamedFilterChainScope::set(chains);
  }
  ~ScopedNamedFilterChainSetter() { NamedFilterChainScope::clear(); }

  // Non-copyable and non-movable.
  ScopedNamedFilterChainSetter(const ScopedNamedFilterChainSetter&) = delete;
  ScopedNamedFilterChainSetter& operator=(const ScopedNamedFilterChainSetter&) = delete;
};

struct HttpFilterActionContext {
  // Identify whether the filter is in downstream filter chain or upstream filter chain.
  const bool is_downstream_ = true;
  const std::string& stat_prefix_;
  OptRef<Server::Configuration::FactoryContext> factory_context_;
  OptRef<Server::Configuration::UpstreamFactoryContext> upstream_factory_context_;
  OptRef<Server::Configuration::ServerFactoryContext> server_factory_context_;
  // Optional reference to named filter chains defined at HCM level.
  // These can be referenced from composite filter actions via filter_chain_ref.
  NamedFilterChainFactoryMapConstSharedPtr named_filter_chains_;
};

} // namespace Matching
} // namespace Http
} // namespace Envoy
