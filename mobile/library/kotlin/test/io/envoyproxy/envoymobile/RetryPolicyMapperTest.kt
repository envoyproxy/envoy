package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RetryPolicyMapperTest {

  @Test
  fun `all retry policy properties should all be encoded`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.RETRIABLE_4XX,
            RetryRule.REFUSED_UPSTREAM),
        perRetryTimeoutMs = 9001)

    assertThat(retryPolicy.outboundHeaders()).isEqualTo(mapOf(
        "x-envoy-max-retries" to listOf("123"),
        "x-envoy-retry-on" to listOf("5xx", "gateway-error", "connect-failure", "retriable-4xx", "refused-upstream"),
        "x-envoy-upstream-rq-per-try-timeout-ms" to listOf("9001")
    ))
  }

  @Test
  fun `retry policy without perRetryTimeoutMs should exclude per try time ms header key`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.RETRIABLE_4XX,
            RetryRule.REFUSED_UPSTREAM))

    assertThat(retryPolicy.outboundHeaders()).doesNotContainKey("x-envoy-upstream-rq-per-try-timeout-ms")
  }
}
