package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RequestMapperTest {

  @Test
  fun `method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("POST")
  }

  @Test
  fun `scheme is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":scheme"]).containsExactly("https")

  }

  @Test
  fun `authority is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":authority"]).containsExactly("api.foo.com")
  }

  @Test
  fun `path is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":path"]).containsExactly("/foo")
  }

  @Test
  fun `h1 is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP1)
        .build()
        .outboundHeaders()

    assertThat(requestHeaders["x-envoy-mobile-upstream-protocol"]).containsExactly("http1")
  }

  @Test
  fun `h2 is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
        .build()
        .outboundHeaders()

    assertThat(requestHeaders["x-envoy-mobile-upstream-protocol"]).containsExactly("http2")
  }

  @Test
  fun `same key headers are joined to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addHeader("header_1", "value_a")
        .addHeader("header_1", "value_b")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders["header_1"]).containsExactly("value_a", "value_b")
  }

  @Test
  fun `restricted header keys are filtered out of outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addHeader(":restricted", "value")
        .addHeader("x-envoy-mobile-test", "value")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders).doesNotContainKey(":restricted")
    assertThat(requestHeaders).doesNotContainKey("x-envoy-mobile-test")
  }

  @Test
  fun `restricted headers are not overwritten in outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP2)
        .addHeader(":scheme", "override")
        .addHeader(":authority", "override")
        .addHeader(":path", "override")
        .addHeader("x-envoy-mobile-upstream-protocol", "override")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":scheme"]).containsExactly("https")
    assertThat(requestHeaders[":authority"]).containsExactly("api.foo.com")
    assertThat(requestHeaders[":path"]).containsExactly("/foo")
    assertThat(requestHeaders["x-envoy-mobile-upstream-protocol"]).containsExactly("http2")
  }

  @Test
  fun `request headers are forwarded to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addHeader("header_1", "value_a")
        .addHeader("header_2", "value_b")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders["header_1"]).containsExactly("value_a")
    assertThat(requestHeaders["header_2"]).containsExactly("value_b")
  }

  @Test
  fun `retry policy is added to outbound request headers`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.RETRIABLE_4XX,
            RetryRule.REFUSED_UPSTREAM),
        perRetryTimeoutMS = 9001)
    val retryPolicyHeaders = retryPolicy.outboundHeaders()

    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addRetryPolicy(retryPolicy)
        .build()
        .outboundHeaders()

    assertThat(requestHeaders).containsAllEntriesOf(retryPolicyHeaders)
  }

  @Test
  fun `retry policy headers are not overwritten in outbound request headers`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.RETRIABLE_4XX,
            RetryRule.REFUSED_UPSTREAM),
        perRetryTimeoutMS = 9001)
    val retryPolicyHeaders = retryPolicy.outboundHeaders()

    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .addHeader("x-envoy-max-retries", "override")
        .addRetryPolicy(retryPolicy)
        .build()
        .outboundHeaders()

    assertThat(requestHeaders).containsAllEntriesOf(retryPolicyHeaders)
  }

  @Test
  fun `delete method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.DELETE, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("DELETE")
  }

  @Test
  fun `get method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.GET, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("GET")
  }

  @Test
  fun `head method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.HEAD, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("HEAD")
  }

  @Test
  fun `options method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.OPTIONS, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("OPTIONS")
  }

  @Test
  fun `patch method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.PATCH, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("PATCH")
  }

  @Test
  fun `post method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("POST")
  }

  @Test
  fun `put method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.PUT, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("PUT")
  }

  @Test
  fun `trace method is added to outbound request headers`() {
    val requestHeaders = RequestBuilder(method = RequestMethod.TRACE, scheme = "https", authority = "api.foo.com", path = "/foo")
        .build()
        .outboundHeaders()

    assertThat(requestHeaders[":method"]).containsExactly("TRACE")
  }
}
