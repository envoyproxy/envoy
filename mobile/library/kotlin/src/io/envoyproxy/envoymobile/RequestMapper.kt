package io.envoyproxy.envoymobile

internal fun Request.outboundHeaders(): Map<String, String> {
  val validHeaders = headers
    .filter { entry -> !entry.key.startsWith(":") }
    .mapValues { entry -> entry.value.joinToString(separator = ",") }

  val retryPolicyHeaders = retryPolicy?.outboundHeaders() ?: emptyMap()

  val result = mutableMapOf<String, String>()
  result.putAll(validHeaders)
  result.putAll(retryPolicyHeaders)
  result[":method"] = method.stringValue()
  result[":scheme"] = scheme
  result[":authority"] = authority
  result[":path"] = path

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
