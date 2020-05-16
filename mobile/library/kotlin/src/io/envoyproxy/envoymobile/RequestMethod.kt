package io.envoyproxy.envoymobile

import java.lang.IllegalArgumentException

/**
 * Represents an HTTP request method.
 */
enum class RequestMethod(internal val stringValue: String) {
  DELETE("DELETE"),
  GET("GET"),
  HEAD("HEAD"),
  OPTIONS("OPTIONS"),
  PATCH("PATCH"),
  POST("POST"),
  PUT("PUT"),
  TRACE("TRACE");

  companion object {
    internal fun enumValue(stringRepresentation: String): RequestMethod {
      return when (stringRepresentation) {
        "DELETE" -> RequestMethod.DELETE
        "GET" -> RequestMethod.GET
        "HEAD" -> RequestMethod.HEAD
        "OPTIONS" -> RequestMethod.OPTIONS
        "PATCH" -> RequestMethod.PATCH
        "POST" -> RequestMethod.POST
        "PUT" -> RequestMethod.PUT
        "TRACE" -> RequestMethod.TRACE
        else -> throw IllegalArgumentException("invalid value $stringRepresentation")
      }
    }
  }
}
