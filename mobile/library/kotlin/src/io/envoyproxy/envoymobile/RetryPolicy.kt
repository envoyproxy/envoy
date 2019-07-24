package io.envoyproxy.envoymobile

/**
 * Specifies how a request may be retried, containing one or more rules.
 *
 * @param maxRetryCount Maximum number of retries that a request may be performed.
 * @param retryOn Whitelist of rules used for retrying.
 * @param perRetryTimeoutMs Timeout (in milliseconds) to apply to each retry.
 */
data class RetryPolicy(
  val maxRetryCount: Int,
  val retryOn: List<RetryRule>,
  val perRetryTimeoutMs: Long? = null
)

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
