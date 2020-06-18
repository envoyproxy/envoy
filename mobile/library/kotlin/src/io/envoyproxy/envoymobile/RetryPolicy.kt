package io.envoyproxy.envoymobile

import java.lang.IllegalArgumentException

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
      perRetryTimeoutMS > totalUpstreamTimeoutMS && totalUpstreamTimeoutMS != 0L
    ) {
      throw IllegalArgumentException("Per-retry timeout cannot be less than total timeout")
    }
  }

  companion object {
    /**
     * Initialize the retry policy from a set of headers.
     *
     * @param headers: The headers with which to initialize the retry policy.
     */
    internal fun from(headers: RequestHeaders): RetryPolicy? {
      val maxRetries = headers.value("x-envoy-max-retries")?.first()?.toIntOrNull() ?: return null

      return RetryPolicy(
        maxRetries,
        headers.value("x-envoy-retry-on")
          ?.map { retryOn -> RetryRule.enumValue(retryOn) }?.filterNotNull() ?: emptyList(),
        headers.value("x-envoy-retriable-status-codes")
          ?.map { statusCode -> statusCode.toIntOrNull() }?.filterNotNull() ?: emptyList(),
        headers.value("x-envoy-upstream-rq-per-try-timeout-ms")?.firstOrNull()?.toLongOrNull(),
        headers.value("x-envoy-upstream-rq-timeout-ms")?.firstOrNull()?.toLongOrNull()
      )
    }
  }
}

/**
 * Rules that may be used with `RetryPolicy`.
 * See the `x-envoy-retry-on` Envoy header for documentation.
 */
enum class RetryRule(internal val stringValue: String) {
  STATUS_5XX("5xx"),
  GATEWAY_ERROR("gateway-error"),
  CONNECT_FAILURE("connect-failure"),
  REFUSED_STREAM("refused-stream"),
  RETRIABLE_4XX("retriable-4xx"),
  RETRIABLE_HEADERS("retriable-headers"),
  RESET("reset");

  companion object {
    internal fun enumValue(stringRepresentation: String): RetryRule? {
      return when (stringRepresentation) {
        "5xx" -> STATUS_5XX
        "gateway-error" -> GATEWAY_ERROR
        "connect-failure" -> CONNECT_FAILURE
        "refused-stream" -> REFUSED_STREAM
        "retriable-4xx" -> RETRIABLE_4XX
        "retriable-headers" -> RETRIABLE_HEADERS
        "reset" -> RESET
        // This is mapped to null because this string value is added to headers automatically
        // in RetryPolicy.outboundHeaders()
        "retriable-status-codes" -> null
        else -> throw IllegalArgumentException("invalid value $stringRepresentation")
      }
    }
  }
}
