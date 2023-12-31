#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/stats.h"

#include "source/common/protobuf/message_validator_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

/**
 * An admission controller that determines if a retry should be allowed for a single stream.
 * The controller is instantiated once for each stream, and is shared by all tries.
 * The controller should be destroyed once a winning upstream try has been chosen to be proxied
 downstream.
 *
 * A try is defined as a single attempt to send a request to the upstream.
 * The lifecycle of a try is as follows:
 * 1. The try is scheduled (possibly immediately)
 * 2. The try is started, i.e. dispatched to an upstream connection pool.
 * 3. (optional) The try is considered successful,
      i.e. it is selected as the winning try to be proxied downstream.
 * 3. The try is completed, either successfully or not:
 *    a. Finished successfully (selected as winning try and upstream component complete)
 *    b. Aborted due to started retry
 *    c. Aborted due to non-retry reasons
 *
 * When the stream admission controller is destroyed, all outstanding tries are considered aborted.
 *
 * The controller needs to track the lifecycle of each try, retry or otherwise, so that it can
 * make informed admission decisions, both within this stream and for other streams routed to the
 same cluster.
 */
class RetryStreamAdmissionController {
public:
  virtual ~RetryStreamAdmissionController() = default;

  /**
   * Called when a try is started, i.e. issued to the upstream.
   * The initial try is started immediately after the stream is routed to the cluster.
   * Retries are only said to have started after any backoff elapses and the upstream
   * request is sent to the connection pool.
   *
   * @param attempt_number the number of the try that started.
   */
  virtual void onTryStarted(uint64_t attempt_number) PURE;

  /**
   * Called when a try has succeeded.
   * A try is said to have succeeded if it is picked as the winning try to
   * be streamed to the downstream.
   * This should be called immediately once the try is chosen
   * as the final winning try for downstream consumption.
   *
   * This method must be called at most once, as only at most one try can win.
   *
   * @param attempt_number the number of the try that succeeded.
   */
  virtual void onTrySucceeded(uint64_t attempt_number) PURE;

  /**
   * Called when an upstream try previously marked as successful has
   * completed the entire upstream component, and is no longer using any upstream resources.
   *
   * Note that the downstream may not yet be finished.
   */
  virtual void onSuccessfulTryFinished() PURE;

  /**
   * Called when a try has been aborted and the abort isn't due to an admitted retry for the try.
   *
   * There are many reasons why a try may be aborted, including:
   * - The stream is reset by the downstream.
   * - Multiple tries are in-flight and one has succeeded.
   * - The retry buffer for replaying request data becomes full while a retry is scheduled but not
   * yet started.
   * - The upstream cluster is removed between tries when this try is scheduled, but not yet
   * started.
   * - The try had a stream timeout or reset.
   *
   * An aborted try is considered done, and will never be considered successful.
   * However, an aborted try may be retried, for instance in the case of an upstream reset retry.
   *
   * @param attempt_number the number of the try that was aborted.
   */
  virtual void onTryAborted(uint64_t attempt_number) PURE;

  /**
   * Called when the router wants to retry a previously-started try.
   * If a retry is admitted, then it must be attempted.
   * A try may only be retried once.
   *
   * @param previous_attempt_number the number of the try that the router wants to re-attempt.
   * @param retry_attempt_number the number of the new retry attempt, valid only if the retry is
   admitted.
   *                             If admitted, the new try will begin in the "scheduled" phase.
   * @param abort_previous_on_retry true if and only if the previous try isn't already aborted,
                                    and accepting the proposed retry implies that the previous
                                    try becomes aborted.
   * @return true if and only if the retry is admitted.
   */
  virtual bool isRetryAdmitted(uint64_t previous_attempt_number, uint64_t retry_attempt_number,
                               bool abort_previous_on_retry) PURE;
};

using RetryStreamAdmissionControllerPtr = std::unique_ptr<RetryStreamAdmissionController>;

/**
 * An admission controller that determines if a retry should be allowed for a stream.
 * The controller is instantiated once for the whole cluster, and is shared by all streams
 * (across multiple threads).
 *
 * The role of the controller is to hand out retry permits to streams that are eligible for retries.
 * On future retries, the stream will attempt to renew the permit before attempting the retry.
 *
 * Clients must ensure that no retries are attempted without a valid permit.
 */
class RetryAdmissionController {
public:
  virtual ~RetryAdmissionController() = default;

  /**
   * Create a new admission controller for a stream.
   * This must be called when a stream is first routed to a cluster,
   * before the first try is attempted.
   *
   * @param request_stream_info the stream info for the downstream.
   */
  virtual RetryStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo& request_stream_info) PURE;
};

using RetryAdmissionControllerSharedPtr = std::shared_ptr<RetryAdmissionController>;

/**
 * Factory for RetryAdmissionController.
 */
class RetryAdmissionControllerFactory : public Config::TypedFactory {
public:
  virtual RetryAdmissionControllerSharedPtr
  createAdmissionController(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            Runtime::Loader& runtime, std::string runtime_key_prefix,
                            ClusterCircuitBreakersStats cb_stats) PURE;

  std::string category() const override { return "envoy.retry_admission_control"; }
};

class AdmissionControl {
public:
  virtual ~AdmissionControl() = default;

  virtual RetryAdmissionControllerSharedPtr retry() PURE;
};

} // namespace Upstream
} // namespace Envoy
