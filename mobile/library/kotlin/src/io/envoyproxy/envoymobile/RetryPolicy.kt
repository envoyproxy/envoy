package io.envoyproxy.envoymobile

/**
 * Specifies how a request may be retried, containing one or more rules.
 *
 * @param maxRetryCount Maximum number of retries that a request may be performed.
 * @param retryOn Whitelist of rules used for retrying.
 * @param retryStatusCodes Additional list of status codes that should be retried.
 * @param perRetryTimeoutMS Timeout (in milliseconds) to apply to each retry.
 * Must be <= `totalUpstreamTimeoutMS` if it's a positive number.
 * @param totalUpstreamTimeoutMS Total timeout (in milliseconds) that includes all retries.
 * Spans the point at which the entire downstream request has been processed and when the
 * upstream response has been completely processed. Null or 0 may be specified to disable it.
 */
data class RetryPolicy(
    val maxRetryCount: Int,
    val retryOn: List<RetryRule>,
    val retryStatusCodes: List<Int> = emptyList(),
    val perRetryTimeoutMS: Long? = null,
    val totalUpstreamTimeoutMS: Long? = 15000
) {
  init {
    if (perRetryTimeoutMS != null && totalUpstreamTimeoutMS != null &&
        perRetryTimeoutMS > totalUpstreamTimeoutMS && totalUpstreamTimeoutMS != 0L) {
      throw IllegalArgumentException("Per-retry timeout cannot be less than total timeout")
    }
  }
}

/**
 * Rules that may be used with `RetryPolicy`.
 * See the `x-envoy-retry-on` Envoy header for documentation.
 */
enum class RetryRule {
  STATUS_5XX,
  GATEWAY_ERROR,
  CONNECT_FAILURE,
  REFUSED_STREAM,
  RETRIABLE_4XX,
  RETRIABLE_HEADERS,
  RESET,
}
