package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit

class ResponseHandlerTest {
  @Test(timeout = 100L)
  fun `parsing status code from headers returns first status code`() {
    val countDownLatch = CountDownLatch(1)
    val headers = mapOf(":status" to listOf("204", "200"), "other" to listOf("1"))
    val responseHandler = ResponseHandler(Executor {})

    responseHandler.onHeaders { _, statusCode, _ ->
      assertThat(statusCode).isEqualTo(204)
      countDownLatch.countDown()
    }
    responseHandler.underlyingCallbacks.onHeaders(headers, false)
    countDownLatch.await(1L, TimeUnit.SECONDS)
  }

  @Test(timeout = 100L)
  fun `parsing invalid status code from headers returns 0`() {
    val countDownLatch = CountDownLatch(1)
    val headers = mapOf(":status" to listOf("invalid"), "other" to listOf("1"))
    val responseHandler = ResponseHandler(Executor {})

    responseHandler.onHeaders { _, statusCode, _ ->
      assertThat(statusCode).isEqualTo(0)
      countDownLatch.countDown()
    }
    responseHandler.underlyingCallbacks.onHeaders(headers, false)
    countDownLatch.await(1L, TimeUnit.SECONDS)
  }

  @Test(timeout = 100L)
  fun `parsing missing status code from headers returns 0`() {
    val countDownLatch = CountDownLatch(1)
    val headers = mapOf("other" to listOf("1"))
    val responseHandler = ResponseHandler(Executor { })

    responseHandler.onHeaders { _, statusCode, _ ->
      assertThat(statusCode).isEqualTo(0)
      countDownLatch.countDown()
    }
    responseHandler.underlyingCallbacks.onHeaders(headers, false)
    countDownLatch.await(1L, TimeUnit.SECONDS)
  }
}
