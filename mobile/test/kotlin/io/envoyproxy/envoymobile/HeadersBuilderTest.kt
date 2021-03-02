package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class HeadersBuilderTest {
  @Test
  fun `adding new header adds to list of header keys`() {
    val headers = RequestHeadersBuilder(mutableMapOf())
      .add("x-foo", "1")
      .add("x-foo", "2")
      .build()
    assertThat(headers.value("x-foo")).containsExactly("1", "2")
  }

  @Test
  fun `removing specific header key removes all of its values`() {
    val headers = RequestHeadersBuilder(mutableMapOf())
      .add("x-foo", "1")
      .add("x-foo", "2")
      .remove("x-foo")
      .build()

    assertThat(headers.allHeaders()).doesNotContainKey("x-foo")
  }

  @Test
  fun `removing specific header key does not remove other keys`() {
    val headers = RequestHeadersBuilder(mutableMapOf())
      .add("x-foo", "123")
      .add("x-bar", "abc")
      .build()
    assertThat(headers.value("x-foo")).containsExactly("123")
    assertThat(headers.value("x-bar")).containsExactly("abc")
  }

  @Test
  fun `setting header replaces existing headers with matching name`() {
    val headers = RequestHeadersBuilder(mutableMapOf())
      .add("x-foo", "123")
      .set("x-foo", mutableListOf("abc"))
      .build()
    assertThat(headers.value("x-foo")).containsExactly("abc")
  }
}
