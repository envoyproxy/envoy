package io.envoyproxy.envoymobile

/**
 * Specifies how a request may be retried, containing one or more rules.
 *
 * @param maxRetryCount Maximum number of retries that a request may be performed.
 * @param retryOn Whitelist of rules used for retrying.
 * @param perRetryTimeoutMS Timeout (in milliseconds) to apply to each retry.
 *                          Must be <= `totalUpstreamTimeoutMS`.
 * @param totalUpstreamTimeoutMS Total timeout (in milliseconds) that includes all retries.
 * Spans the point at which the entire downstream request has been processed and when the
 * upstream response has been completely processed.
 */
data class RetryPolicy(
  val maxRetryCount: Int,
  val retryOn: List<RetryRule>,
  val perRetryTimeoutMS: Long? = null,
  val totalUpstreamTimeoutMS: Long = 15000
) {
  init {
    if (perRetryTimeoutMS != null && perRetryTimeoutMS > totalUpstreamTimeoutMS) {
      throw IllegalArgumentException("Per-retry timeout must be <= total timeout")
    }
  }
}

/**
 * These are retry rules specified in Envoy's router filter.
 * @see <a href="https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/router_filter#x-envoy-retry-on">x-envoy-retry-on</a>
 */
enum class RetryRule {
  STATUS_5XX,
  GATEWAY_ERROR,
  CONNECT_FAILURE,
  RETRIABLE_4XX,
  REFUSED_UPSTREAM,
}
