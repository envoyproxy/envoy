package io.envoyproxy.envoymobile

/**
 * Error type containing information on failures reported by Envoy.
 *
 * @param errorCode internal error code associated with the exception that occurred.
 * @param message a description of what exception that occurred.
 * @param attemptCount an optional number of times an operation was attempted before firing this
 *   error.
 * @param cause an optional cause for the exception.
 */
class EnvoyError(
  val errorCode: Int,
  val message: String,
  val attemptCount: Int? = null,
  val cause: Throwable? = null
)
