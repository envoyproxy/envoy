package io.envoyproxy.envoymobile.shared

// Response is a class to handle HTTP responses.
sealed class Response {
  fun fold(success: (Success) -> Unit, failure: (Failure) -> Unit) = when (this) {
    is Success -> success(this)
    is Failure -> failure(this)
  }
}

data class Success(val title: String, val header: String) : Response()

data class Failure(val message: String) : Response()



