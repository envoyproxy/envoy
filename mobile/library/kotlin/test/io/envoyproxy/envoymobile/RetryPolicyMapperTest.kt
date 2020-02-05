package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RetryPolicyMapperTest {

  @Test
  fun `all retry policy properties should all be encoded`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 3,
        retryOn = listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.RETRIABLE_4XX,
            RetryRule.REFUSED_UPSTREAM),
        perRetryTimeoutMS = 15000,
        totalUpstreamTimeoutMS = 60000)

    assertThat(retryPolicy.outboundHeaders()).isEqualTo(mapOf(
        "x-envoy-max-retries" to listOf("3"),
        "x-envoy-retry-on" to listOf("5xx", "gateway-error", "connect-failure", "retriable-4xx", "refused-upstream"),
        "x-envoy-upstream-rq-per-try-timeout-ms" to listOf("15000"),
        "x-envoy-upstream-rq-timeout-ms" to listOf("60000")
    ))
  }

  @Test
  fun `retry policy without perRetryTimeoutMS should exclude per try time ms header key`() {
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

  @Test
  fun `retry policy with null totalUpstreamTimeoutMS should include zero ms header`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(RetryRule.STATUS_5XX),
        totalUpstreamTimeoutMS = null)

        assertThat(retryPolicy.outboundHeaders()).isEqualTo(mapOf(
          "x-envoy-max-retries" to listOf("123"),
          "x-envoy-retry-on" to listOf("5xx"),
          "x-envoy-upstream-rq-timeout-ms" to listOf("0")
      ))
  }

  @Test(expected = IllegalArgumentException::class)
  fun `throws error when per-retry timeout is larger than total timeout`() {
    val retryPolicy = RetryPolicy(
        maxRetryCount = 3,
        retryOn = listOf(RetryRule.STATUS_5XX),
        perRetryTimeoutMS = 2,
        totalUpstreamTimeoutMS = 1)
  }
}
