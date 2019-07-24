package library.kotlin.test.io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.RetryPolicy
import io.envoyproxy.envoymobile.RetryRule
import io.envoyproxy.envoymobile.headers
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

    assertThat(retryPolicy.headers()).isEqualTo(mapOf(
        "x-envoy-max-retries" to "123",
        "x-envoy-retry-on" to "5xx,gateway-error,connect-failure,retriable-4xx,refused-upstream",
        "x-envoy-upstream-rq-per-try-timeout-ms" to "9001"
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

    assertThat(retryPolicy.headers()).doesNotContainKey("x-envoy-upstream-rq-per-try-timeout-ms")
  }
}
