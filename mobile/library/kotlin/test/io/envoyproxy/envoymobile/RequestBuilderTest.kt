package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RequestBuilderTest {

  @Test
  fun `adding retry policy should have policy present in request`() {

    val retryPolicy = RetryPolicy(maxRetryCount = 23, retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.CONNECT_FAILURE), perRetryTimeoutMS = 1234)
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addRetryPolicy(retryPolicy)
        .build()

    assertThat(request.retryPolicy).isEqualTo(RetryPolicy(23, listOf(RetryRule.STATUS_5XX, RetryRule.CONNECT_FAILURE), 1234))
  }

  @Test
  fun `not adding retry policy should have null body in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .build()

    assertThat(request.retryPolicy).isNull()
  }

  @Test
  fun `adding upstream http protocol should have hint present in request`() {

    val retryPolicy = RetryPolicy(maxRetryCount = 23, retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.CONNECT_FAILURE), perRetryTimeoutMS = 1234)
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
        .build()

    assertThat(request.upstreamHttpProtocol).isEqualTo(UpstreamHttpProtocol.HTTP2)
  }

  @Test
  fun `not adding upstream http protocol should have null upstream protocol in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .build()

    assertThat(request.upstreamHttpProtocol).isNull()
  }

  @Test
  fun `adding new headers should append to the list of header keys`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .build()

    assertThat(request.headers["header_a"]).contains("value_a1")
  }

  @Test
  fun `removing headers should clear headers in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .removeHeaders("header_a")
        .build()

    assertThat(request.headers).doesNotContainKey("header_a")
  }

  @Test
  fun `removing a specific header value should not be in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .removeHeader("header_a", "value_a1")
        .build()

    assertThat(request.headers["header_a"]).doesNotContain("value_a1")
  }

  @Test
  fun `removing a specific header value should keep the other header values in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .removeHeader("header_a", "value_a1")
        .build()

    assertThat(request.headers["header_a"]).contains("value_a2")
  }

  @Test
  fun `adding a specific header value should keep the other header values in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .build()

    assertThat(request.headers["header_a"]).containsExactly("value_a1", "value_a2")
  }

  @Test
  fun `removing all header values should remove header list in request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .removeHeader("header_a", "value_a1")
        .build()

    assertThat(request.headers["header_a"]).isNull()
  }
}
