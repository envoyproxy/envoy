package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class ResponseHeadersTest {
  @Test
  fun `parsing status code from headers returns first status`() {
    val headers = ResponseHeaders(mapOf(":status" to listOf("204", "200")))
    assertThat(headers.httpStatus).isEqualTo(204)
  }

  @Test
  fun `parsing invalid status code returns null`() {
    val headers = ResponseHeaders(mapOf(":status" to listOf("invalid"), "other" to listOf("1")))
    assertThat(headers.httpStatus).isNull()
  }

  @Test
  fun `parsing missing status code returns null`() {
    val headers = ResponseHeaders(emptyMap())
    assertThat(headers.httpStatus).isNull()
  }

  @Test
  fun `adding HTTP status code sets the appropriate header`() {
    val headers = ResponseHeadersBuilder()
      .addHttpStatus(200)
      .build()
    assertThat(headers.value(":status")).containsExactly("200")
  }
}
