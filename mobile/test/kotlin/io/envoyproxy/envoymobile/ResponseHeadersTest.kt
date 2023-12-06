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
  fun `parsing invalid status string returns null`() {
    val headers = ResponseHeaders(mapOf(":status" to listOf("invalid"), "other" to listOf("1")))
    assertThat(headers.httpStatus).isNull()
  }

  @Test
  fun `parsing negative status returns null`() {
    val headers = ResponseHeaders(mapOf(":status" to listOf("-123"), "other" to listOf("1")))
    assertThat(headers.httpStatus).isNull()
  }

  @Test
  fun `parsing missing status code returns null`() {
    val headers = ResponseHeaders(emptyMap())
    assertThat(headers.httpStatus).isNull()
  }

  @Test
  fun `adding HTTP status code sets the appropriate header`() {
    val headers = ResponseHeadersBuilder().addHttpStatus(200).build()
    assertThat(headers.value(":status")).containsExactly("200")
  }

  @Test
  fun `adding negative HTTP status code no-ops`() {
    val headers = ResponseHeadersBuilder().addHttpStatus(-123).build()
    assertThat(headers.value(":status")).isNull()
  }

  @Test
  fun `header lookup is a case-insensitive operation`() {
    val headers = ResponseHeaders(mapOf("FoO" to listOf("123")))
    assertThat(headers.value("FoO")).isEqualTo(listOf("123"))
    assertThat(headers.value("foo")).isEqualTo(listOf("123"))
  }
}
