#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class MetadataFetcher;
using MetadataFetcherPtr = std::unique_ptr<MetadataFetcher>;

/**
 * MetadataFetcher interface can be used to retrieve AWS Metadata from various providers.
 * An instance of this interface is designed to retrieve one AWS Metadata at a time.
 * The implementation of AWS Metadata Fetcher is similar to JwksFetcher.
 */

class MetadataFetcher {
public:
  class MetadataReceiver {
  public:
    enum class Failure {
      /* A network error occurred causing AWS Metadata retrieval failure. */
      Network,
      /* A failure occurred when trying to parse the retrieved AWS Metadata data. */
      InvalidMetadata,
      /* A missing config causing AWS Metadata retrieval failure. */
      MissingConfig,
    };

    // Metadata fetcher begins in "FirstRefresh" and stays there until first success, then reverts
    // to standard cache duration timing. "FirstRefresh" state will cause credential refresh at 2
    // sec, doubling to a maximum of 30 sec until successful.
    enum class RefreshState {
      FirstRefresh,
      Ready,
    };

    virtual ~MetadataReceiver() = default;

    /**
     * @brief Successful retrieval callback of returned AWS Metadata.
     * @param body Fetched AWS Metadata.
     */
    virtual void onMetadataSuccess(const std::string&& body) PURE;

    /**
     * @brief Retrieval error callback.
     * @param reason the failure reason.
     */
    virtual void onMetadataError(Failure reason) PURE;
  };

  virtual ~MetadataFetcher() = default;

  /**
   * @brief Cancel any in-flight request.
   */
  virtual void cancel() PURE;

  /**
   * @brief Retrieve a AWS Metadata from a remote HTTP host.
   * At most one outstanding request may be in-flight.
   * i.e. from the invocation of `fetch()` until either
   * a callback or `cancel()` is invoked, no additional
   * `fetch()` may be issued. The URI to fetch is to pre
   * determined based on the credentials provider source.
   *
   * @param receiver the receiver of the fetched AWS Metadata or error
   */
  virtual void fetch(Http::RequestMessage& message, Tracing::Span& parent_span,
                     MetadataReceiver& receiver) PURE;

  /**
   * @brief Return MetadataReceiver Failure enum as a string.
   *
   * @return absl::string_view
   */
  virtual absl::string_view failureToString(MetadataReceiver::Failure) PURE;

  /**
   * @brief Factory method for creating a Metadata Fetcher.
   *
   * @param cm the cluster manager to use during AWS Metadata retrieval
   * @param provider the AWS Metadata provider
   * @return a MetadataFetcher instance
   */
  static MetadataFetcherPtr create(Upstream::ClusterManager& cm, absl::string_view cluster_name);
};
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
