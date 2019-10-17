package io.envoyproxy.envoymobile

/**
 * Converts the retry policy to a set of headers recognized by Envoy.
 *
 * @return The header representation of the retry policy.
 */
internal fun RetryPolicy.outboundHeaders(): Map<String, List<String>> {

  val headers = mutableMapOf(
      "x-envoy-max-retries" to listOf("$maxRetryCount"),
      "x-envoy-retry-on" to retryOn.map { elm -> elm.stringValue() },
      "x-envoy-upstream-rq-timeout-ms" to listOf("$totalUpstreamTimeoutMS")
  )

  if (perRetryTimeoutMS != null) {
    headers["x-envoy-upstream-rq-per-try-timeout-ms"] = listOf("$perRetryTimeoutMS")
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
