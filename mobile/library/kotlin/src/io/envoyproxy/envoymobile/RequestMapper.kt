package io.envoyproxy.envoymobile

internal fun Request.outboundHeaders(): Map<String, List<String>> {
  val retryPolicyHeaders = retryPolicy?.outboundHeaders() ?: emptyMap()

  val result = mutableMapOf<String, List<String>>()
  result.putAll(headers.filter { entry -> !entry.key.startsWith(":") })
  result.putAll(retryPolicyHeaders)
  result[":method"] = listOf(method.stringValue())
  result[":scheme"] = listOf(scheme)
  result[":authority"] = listOf(authority)
  result[":path"] = listOf(path)

  return result
}

private fun RequestMethod.stringValue(): String {
  return when (this) {
    RequestMethod.DELETE -> "DELETE"
    RequestMethod.GET -> "GET"
    RequestMethod.HEAD -> "HEAD"
    RequestMethod.OPTIONS -> "OPTIONS"
    RequestMethod.PATCH -> "PATCH"
    RequestMethod.POST -> "POST"
    RequestMethod.PUT -> "PUT"
    RequestMethod.TRACE -> "TRACE"
  }
}
