package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class HeadersBuilderTest {
  @Test
  fun `adding new header adds to list of header keys`() {
    val headers = RequestHeadersBuilder(mutableMapOf()).add("x-foo", "1").add("x-foo", "2").build()
    assertThat(headers.value("x-foo")).containsExactly("1", "2")
  }

  @Test
  fun `adding header performs a case-insensitive header name lookup`() {
    val headers = RequestHeadersBuilder(mutableMapOf()).add("fOo", "abc").add("foo", "123").build()
    assertThat(headers.caseSensitiveHeaders()).isEqualTo(mapOf("fOo" to listOf("abc", "123")))
  }

  @Test
  fun `removing specific header key removes all of its values`() {
    val headers =
      RequestHeadersBuilder(mutableMapOf())
        .add("x-foo", "1")
        .add("x-foo", "2")
        .remove("x-foo")
        .build()
    assertThat(headers.caseSensitiveHeaders()).doesNotContainKey("x-foo")
  }

  @Test
  fun `removing specific header key does not remove other keys`() {
    val headers =
      RequestHeadersBuilder(mutableMapOf()).add("x-foo", "123").add("x-bar", "abc").build()
    assertThat(headers.value("x-foo")).containsExactly("123")
    assertThat(headers.value("x-bar")).containsExactly("abc")
  }

  @Test
  fun `removing specific header key performs a case-insensitive header name lookup`() {
    val headers =
      RequestHeadersBuilder(mutableMapOf()).set("foo", mutableListOf("123")).remove("fOo").build()
    assertThat(headers.caseSensitiveHeaders()).isEmpty()
  }

  @Test
  fun `setting header replaces existing headers with matching name`() {
    val headers =
      RequestHeadersBuilder(mutableMapOf())
        .add("x-foo", "123")
        .set("x-foo", mutableListOf("abc"))
        .build()
    assertThat(headers.value("x-foo")).containsExactly("abc")
  }

  @Test
  fun `setting header replaces performs a case-insensitive header name lookup`() {
    val headers =
      RequestHeadersBuilder(mapOf())
        .set("foo", mutableListOf("123"))
        .set("fOo", mutableListOf("abc"))
        .build()
    assertThat(headers.caseSensitiveHeaders()).isEqualTo(mapOf("fOo" to listOf("abc")))
  }

  @Test
  fun `test initialization is case-insensitive, preserves casing and processes headers in alphabetical order`() {
    val headers =
      RequestHeadersBuilder(mutableMapOf("a" to mutableListOf("456"), "A" to mutableListOf("123")))
        .build()
    assertThat(headers.caseSensitiveHeaders()).isEqualTo(mapOf("A" to listOf("123", "456")))
  }

  @Test
  fun `test restricted headers are not settable`() {
    val headers =
      RequestHeadersBuilder(method = RequestMethod.GET, authority = "example.com", path = "/")
        .add("host", "example.com")
        .add("Host", "example.com")
        .add("hostWithSuffix", "foo.bar")
        .set(":scheme", mutableListOf("http"))
        .set(":path", mutableListOf("/nope"))
        .build()
        .caseSensitiveHeaders()
    val expected =
      mapOf(
        ":authority" to listOf("example.com"),
        ":path" to listOf("/"),
        ":method" to listOf("GET"),
        ":scheme" to listOf("https"),
        "hostWithSuffix" to listOf("foo.bar"),
      )
    assertThat(headers).isEqualTo(expected)
  }
}
