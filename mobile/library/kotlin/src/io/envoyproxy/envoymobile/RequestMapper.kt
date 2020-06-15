package io.envoyproxy.envoymobile

internal fun Request.outboundHeaders(): Map<String, List<String>> {
  val retryPolicyHeaders = retryPolicy?.outboundHeaders() ?: emptyMap()

  val result = mutableMapOf<String, List<String>>()
  result.putAll(
    headers.filter { entry ->
      !entry.key.startsWith(":") && !entry.key.startsWith("x-envoy-mobile")
    }
  )
  result.putAll(retryPolicyHeaders)
  result[":method"] = listOf(method.stringValue)
  result[":scheme"] = listOf(scheme)
  result[":authority"] = listOf(authority)
  result[":path"] = listOf(path)

  if (upstreamHttpProtocol != null) {
    result["x-envoy-mobile-upstream-protocol"] = listOf(upstreamHttpProtocol.stringValue)
  }

  return result
}
