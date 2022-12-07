package io.envoyproxy.envoymobile

/**
 * Converts the retry policy to a set of headers recognized by Envoy.
 *
 * @return The header representation of the retry policy.
 */
internal fun RetryPolicy.outboundHeaders(): Map<String, List<String>> {
  val upstreamTimeoutMS = totalUpstreamTimeoutMS ?: 0L
  val headers = mutableMapOf(
    "x-envoy-max-retries" to listOf("$maxRetryCount"),
    "x-envoy-upstream-rq-timeout-ms" to listOf("$upstreamTimeoutMS")
  )

  if (perRetryTimeoutMS != null) {
    headers["x-envoy-upstream-rq-per-try-timeout-ms"] = listOf("$perRetryTimeoutMS")
  }

  val retryOn = retryOn.map { elm -> elm.stringValue }.toMutableList()
  if (retryStatusCodes.isNotEmpty()) {
    retryOn.add("retriable-status-codes")
    headers["x-envoy-retriable-status-codes"] = retryStatusCodes.map { value -> "$value" }
  }
  headers["x-envoy-retry-on"] = retryOn
  return headers
}
