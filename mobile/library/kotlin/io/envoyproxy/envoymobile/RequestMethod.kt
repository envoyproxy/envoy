package io.envoyproxy.envoymobile

import java.lang.IllegalArgumentException

/** Represents an HTTP request method. */
enum class RequestMethod(val stringValue: String) {
  DELETE("DELETE"),
  GET("GET"),
  HEAD("HEAD"),
  OPTIONS("OPTIONS"),
  PATCH("PATCH"),
  POST("POST"),
  PUT("PUT"),
  TRACE("TRACE");

  companion object {
    fun enumValue(stringRepresentation: String): RequestMethod {
      return when (stringRepresentation) {
        "DELETE" -> DELETE
        "GET" -> GET
        "HEAD" -> HEAD
        "OPTIONS" -> OPTIONS
        "PATCH" -> PATCH
        "POST" -> POST
        "PUT" -> PUT
        "TRACE" -> TRACE
        else -> throw IllegalArgumentException("invalid value $stringRepresentation")
      }
    }
  }
}
