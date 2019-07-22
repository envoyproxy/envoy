package io.envoyproxy.envoymobile

import java.io.IOException

/**
 * An Envoy Exception class for describing what error may have occurred.
 *
 * @param errorCode internal error code associated with the exception that occurred.
 * @param message a description of what exception that occurred.
 * @param cause an optional cause for the exception.
 */
class EnvoyException(
    val errorCode: Int,
    override val message: String,
    override val cause: Throwable? = null
) : IOException(message, cause)
