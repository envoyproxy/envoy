package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class RetryPolicyMapperTest {
  @Test
  fun `converting to headers with per retry timeout includes all headers`() {
    val retryPolicy =
      RetryPolicy(
        maxRetryCount = 3,
        retryOn =
          listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.REFUSED_STREAM,
            RetryRule.RETRIABLE_4XX,
            RetryRule.RETRIABLE_HEADERS,
            RetryRule.RESET
          ),
        retryStatusCodes = listOf(400, 422, 500),
        perRetryTimeoutMS = 15000,
        totalUpstreamTimeoutMS = 60000
      )

    assertThat(retryPolicy.outboundHeaders())
      .isEqualTo(
        mapOf(
          "x-envoy-max-retries" to listOf("3"),
          "x-envoy-retriable-status-codes" to listOf("400", "422", "500"),
          "x-envoy-retry-on" to
            listOf(
              "5xx",
              "gateway-error",
              "connect-failure",
              "refused-stream",
              "retriable-4xx",
              "retriable-headers",
              "reset",
              "retriable-status-codes"
            ),
          "x-envoy-upstream-rq-per-try-timeout-ms" to listOf("15000"),
          "x-envoy-upstream-rq-timeout-ms" to listOf("60000")
        )
      )
  }

  @Test
  fun `converting from header values delimited with comma yields individual enum values`() {
    val retryPolicy =
      RetryPolicy(
        maxRetryCount = 3,
        retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.GATEWAY_ERROR),
        retryStatusCodes = listOf(400, 422, 500),
        perRetryTimeoutMS = 15000,
        totalUpstreamTimeoutMS = 60000
      )

    val headers =
      RequestHeadersBuilder(
          method = RequestMethod.POST,
          scheme = "https",
          authority = "envoyproxy.io",
          path = "/mock"
        )
        .add("x-envoy-max-retries", "3")
        .add("x-envoy-retriable-status-codes", "400,422,500")
        .add("x-envoy-retry-on", "5xx,gateway-error")
        .add("x-envoy-upstream-rq-per-try-timeout-ms", "15000")
        .add("x-envoy-upstream-rq-timeout-ms", "60000")
        .build()

    val retryPolicyFromHeaders = RetryPolicy.from(headers)!!

    assertThat(retryPolicy).isEqualTo(retryPolicyFromHeaders)
  }

  @Test
  fun `converting to headers without retry timeout excludes per retry timeout header`() {
    val retryPolicy =
      RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(RetryRule.STATUS_5XX, RetryRule.GATEWAY_ERROR)
      )

    assertThat(retryPolicy.outboundHeaders())
      .doesNotContainKey("x-envoy-upstream-rq-per-try-timeout-ms")
  }

  @Test
  fun `converting to headers without upstream timeout includes zero for timeout header`() {
    val retryPolicy =
      RetryPolicy(
        maxRetryCount = 123,
        retryOn = listOf(RetryRule.STATUS_5XX),
        totalUpstreamTimeoutMS = null
      )

    assertThat(retryPolicy.outboundHeaders())
      .isEqualTo(
        mapOf(
          "x-envoy-max-retries" to listOf("123"),
          "x-envoy-retry-on" to listOf("5xx"),
          "x-envoy-upstream-rq-timeout-ms" to listOf("0")
        )
      )
  }

  @Test(expected = IllegalArgumentException::class)
  fun `throws error when per-retry timeout is larger than total timeout`() {
    RetryPolicy(
      maxRetryCount = 3,
      retryOn = listOf(RetryRule.STATUS_5XX),
      perRetryTimeoutMS = 2,
      totalUpstreamTimeoutMS = 1
    )
  }

  @Test
  fun `converting headers without retry status code does not set retriable status code headers`() {
    val retryPolicy =
      RetryPolicy(
        maxRetryCount = 123,
        retryOn =
          listOf(
            RetryRule.STATUS_5XX,
            RetryRule.GATEWAY_ERROR,
            RetryRule.CONNECT_FAILURE,
            RetryRule.REFUSED_STREAM,
            RetryRule.RETRIABLE_4XX,
            RetryRule.RETRIABLE_HEADERS,
            RetryRule.RESET
          ),
        retryStatusCodes = emptyList(),
        totalUpstreamTimeoutMS = null
      )

    val headers = retryPolicy.outboundHeaders()
    assertThat(headers["x-envoy-retriable-status-codes"]).isNull()
    assertThat(headers["x-envoy-retry-on"]).doesNotContain("retriable-status-codes")
  }
}
