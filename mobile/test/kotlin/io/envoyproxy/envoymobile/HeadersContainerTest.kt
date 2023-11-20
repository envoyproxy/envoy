package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class HeadersContainerest {
  @Test
  fun `instantiation preserves all headers from input headers map`() {
    val headers = mapOf("a" to mutableListOf<String>("456"), "b" to mutableListOf<String>("123"))
    val container = HeadersContainer(headers)
    assertThat(container.caseSensitiveHeaders()).isEqualTo(headers)
  }

  @Test
  fun `instantiation with mutable list of values is case-insensitive, preserves casing and processes in alphabetical order`() {
    val container =
      HeadersContainer(
        mapOf("a" to mutableListOf<String>("456"), "A" to mutableListOf<String>("123"))
      )
    assertThat(container.caseSensitiveHeaders()).isEqualTo(mapOf("A" to listOf("123", "456")))
  }

  @Test
  fun `creation with immutable list of values is case-insensitive, preserves casing and processes in alphabetical order`() {
    val container =
      HeadersContainer.create(mapOf("a" to listOf<String>("456"), "A" to listOf<String>("123")))
    assertThat(container.caseSensitiveHeaders()).isEqualTo(mapOf("A" to listOf("123", "456")))
  }

  @Test
  fun `adding header adds to list of headers keys`() {
    val container = HeadersContainer(mutableMapOf())
    container.add("x-foo", "1")
    container.add("x-foo", "2")
    assertThat(container.value("x-foo")).containsExactly("1", "2")
  }

  @Test
  fun `adding header performs a case-insensitive header lookup and preserves header name casing`() {
    val container = HeadersContainer(mapOf())
    container.add("x-FOO", "1")
    container.add("x-foo", "2")

    assertThat(container.value("x-foo")).isEqualTo(listOf("1", "2"))
    assertThat(container.caseSensitiveHeaders()).isEqualTo(mapOf("x-FOO" to listOf("1", "2")))
  }

  @Test
  fun `setting header adds to list of headers keys`() {
    val container = HeadersContainer(mapOf())
    container.set("x-foo", mutableListOf("abc"))

    assertThat(container.value("x-foo")).isEqualTo(listOf("abc"))
  }

  @Test
  fun `setting header overrides previous header values`() {
    val container = HeadersContainer(mapOf())
    container.add("x-FOO", "1")
    container.add("x-foo", "2")
    container.set("x-foo", mutableListOf("3"))

    assertThat(container.value("x-foo")).isEqualTo(listOf("3"))
  }

  @Test
  fun `removing header removes all of its values`() {
    val container = HeadersContainer(mapOf())
    container.add("x-foo", "1")
    container.add("x-foo", "2")
    container.remove("x-foo")

    assertThat(container.value("x-foo")).isNull()
  }

  @Test
  fun `removing header performs case-insensitive header name lookup`() {
    val container = HeadersContainer(mapOf())
    container.add("x-FOO", "1")
    container.add("x-foo", "2")
    container.remove("x-fOo")

    assertThat(container.value("x-foo")).isNull()
  }
}
