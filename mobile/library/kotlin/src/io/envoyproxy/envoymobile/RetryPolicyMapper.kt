package io.envoyproxy.envoymobile

/**
 * Converts the retry policy to a set of headers recognized by Envoy.
 *
 * @return The header representation of the retry policy.
 */
internal fun RetryPolicy.headers(): Map<String, String> {

  val headers = mutableMapOf(
      "x-envoy-max-retries" to "$maxRetryCount",
      "x-envoy-retry-on" to retryOn.joinToString(separator = ",") { elm -> elm.stringValue() }
  )

  if (perRetryTimeoutMs != null) {
    headers["x-envoy-upstream-rq-per-try-timeout-ms"] = "$perRetryTimeoutMs"
  }
  return headers
}

/**
 * Converts the retry rule to its string representation
 *
 * @return The string representation of the retry rule
 */
private fun RetryRule.stringValue(): String {
  return when (this) {
    RetryRule.STATUS_5XX -> "5xx"
    RetryRule.GATEWAY_ERROR -> "gateway-error"
    RetryRule.CONNECT_FAILURE -> "connect-failure"
    RetryRule.RETRIABLE_4XX -> "retriable-4xx"
    RetryRule.REFUSED_UPSTREAM -> "refused-upstream"
  }
}
