package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RequestHeadersBuilderTest {
  @Test
  fun `adds method to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .build()

    assertThat(headers.value(":method")).containsExactly("POST")
    assertThat(headers.method).isEqualTo(RequestMethod.POST)
  }

  @Test
  fun `adds scheme to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .build()

    assertThat(headers.value(":scheme")).containsExactly("https")
    assertThat(headers.scheme).isEqualTo("https")
  }

  @Test
  fun `adds authority to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .build()

    assertThat(headers.value(":authority")).containsExactly("envoyproxy.io")
    assertThat(headers.authority).isEqualTo("envoyproxy.io")
  }

  @Test
  fun `adds path to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .build()

    assertThat(headers.value(":path")).containsExactly("/mock")
    assertThat(headers.path).isEqualTo("/mock")
  }

  @Test
  fun `adds H1 to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP1)
      .build()

    assertThat(headers.value("x-envoy-mobile-upstream-protocol")).containsExactly("http1")
    assertThat(headers.upstreamHttpProtocol).isEqualTo(UpstreamHttpProtocol.HTTP1)
  }

  @Test
  fun `adds H2 to headers`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
      .build()

    assertThat(headers.value("x-envoy-mobile-upstream-protocol")).containsExactly("http2")
    assertThat(headers.upstreamHttpProtocol).isEqualTo(UpstreamHttpProtocol.HTTP2)
  }

  @Test
  fun `joins header values with the same key`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .add("x-foo", "1")
      .add("x-foo", "2")
      .build()

    assertThat(headers.value("x-foo")).containsExactly("1", "2")
  }

  @Test
  fun `cannot publicly add headers with restricted prefix`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .add(":x-foo", "123")
      .add("x-envoy-mobile-foo", "abc")
      .add("host", "example.com")
      .add("hostWithSuffix", "foo.bar")
      .build()

    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey(":x-foo")
    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey("x-envoy-mobile-foo")
    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey("host")
    assertThat(headers.value("hostWithSuffix")).containsExactly("foo.bar")
  }

  @Test
  fun `cannot publicly set headers with restricted prefix`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .set(":x-foo", mutableListOf("123"))
      .set("x-envoy-mobile-foo", mutableListOf("abc"))
      .build()

    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey(":x-foo")
    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey("x-envoy-mobile-foo")
  }

  @Test
  fun `cannot publicly remove headers with restricted prefix`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
      .remove(":path")
      .remove("x-envoy-mobile-upstream-protocol")
      .build()

    assertThat(headers.value(":path")).contains("/mock")
    assertThat(headers.value("x-envoy-mobile-upstream-protocol")).contains("http2")
  }

  @Test
  fun `can internally set headers with restricted prefix`() {
    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .internalSet(":x-foo", mutableListOf("123"))
      .internalSet("x-envoy-mobile-foo", mutableListOf("abc"))
      .build()

    assertThat(headers.value(":x-foo")).containsExactly("123")
    assertThat(headers.value("x-envoy-mobile-foo")).containsExactly("abc")
  }

  @Test
  fun `includes retry policy headers`() {
    val retryPolicy = RetryPolicy(
      maxRetryCount = 123,
      retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.GATEWAY_ERROR),
      perRetryTimeoutMS = 9001
    )
    val retryPolicyHeaders = retryPolicy.outboundHeaders()

    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .addRetryPolicy(retryPolicy)
      .build()

    assertThat(headers.caseSensitiveHeaders()).containsAllEntriesOf(retryPolicyHeaders)
  }

  @Test
  fun `retry policy takes precedence over manually set retry headers`() {
    val retryPolicy = RetryPolicy(
      maxRetryCount = 123,
      retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.GATEWAY_ERROR),
      perRetryTimeoutMS = 9001
    )

    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .add("x-envoy-max-retries", "override")
      .addRetryPolicy(retryPolicy)
      .build()

    assertThat(headers.value("x-envoy-max-retries")).containsExactly("123")
  }

  @Test
  fun `converting to request headers and back maintains equality`() {
    val headers1 = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .build()
    val headers2 = headers1.toRequestHeadersBuilder().build()

    assertThat(headers1.caseSensitiveHeaders()).isEqualTo(headers2.caseSensitiveHeaders())
  }

  @Test
  fun `converting retry policy to headers and back creates the same retry policy`() {
    val retryPolicy = RetryPolicy(
      maxRetryCount = 123,
      retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.GATEWAY_ERROR),
      perRetryTimeoutMS = 9001
    )

    val headers = RequestHeadersBuilder(
      method = RequestMethod.POST, scheme = "https",
      authority = "envoyproxy.io", path = "/mock"
    )
      .addRetryPolicy(retryPolicy)
      .build()

    assertThat(retryPolicy.outboundHeaders()).isEqualTo(RetryPolicy.from(headers)!!.outboundHeaders())
  }

  @Test
  fun `converting request method to string and back creates the same request method`() {
    assertThat(RequestMethod.enumValue(RequestMethod.DELETE.stringValue))
      .isEqualTo(RequestMethod.DELETE)
    assertThat(RequestMethod.enumValue(RequestMethod.GET.stringValue))
      .isEqualTo(RequestMethod.GET)
    assertThat(RequestMethod.enumValue(RequestMethod.HEAD.stringValue))
      .isEqualTo(RequestMethod.HEAD)
    assertThat(RequestMethod.enumValue(RequestMethod.OPTIONS.stringValue))
      .isEqualTo(RequestMethod.OPTIONS)
    assertThat(RequestMethod.enumValue(RequestMethod.PATCH.stringValue))
      .isEqualTo(RequestMethod.PATCH)
    assertThat(RequestMethod.enumValue(RequestMethod.POST.stringValue))
      .isEqualTo(RequestMethod.POST)
    assertThat(RequestMethod.enumValue(RequestMethod.PUT.stringValue))
      .isEqualTo(RequestMethod.PUT)
    assertThat(RequestMethod.enumValue(RequestMethod.TRACE.stringValue))
      .isEqualTo(RequestMethod.TRACE)
  }

  @Test
  fun `converting http protocol to string and back creates the same http protocol`() {
    assertThat(UpstreamHttpProtocol.enumValue(UpstreamHttpProtocol.HTTP1.stringValue))
      .isEqualTo(UpstreamHttpProtocol.HTTP1)
    assertThat(UpstreamHttpProtocol.enumValue(UpstreamHttpProtocol.HTTP2.stringValue))
      .isEqualTo(UpstreamHttpProtocol.HTTP2)
  }
}
