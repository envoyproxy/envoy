package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test


class RequestTest {

  @Test
  fun `requests with the same properties should be equal`() {
    val request1 = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .addHeader("header_b", "value_b1")
        .build()

    val request2 = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .addHeader("header_b", "value_b1")
        .build()

    assertThat(request1).isEqualTo(request2)
  }

  @Test
  fun `requests converted to a builder should build to the same request`() {
    val request = RequestBuilder(method = RequestMethod.POST, scheme = "https", authority = "api.foo.com", path = "foo")
        .addRetryPolicy(RetryPolicy(23, listOf(RetryRule.STATUS_5XX, RetryRule.CONNECT_FAILURE), 1234))
        .addHeader("header_a", "value_a1")
        .addHeader("header_a", "value_a2")
        .addHeader("header_b", "value_b1")
        .build()

    assertThat(request).isEqualTo(request.toBuilder().build())
  }
}
