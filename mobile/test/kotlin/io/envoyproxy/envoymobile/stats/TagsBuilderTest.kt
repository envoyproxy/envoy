package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class TagsBuilderTest {
  @Test
  fun `adds tags to tags`() {
    val tags = TagsBuilder().add("testKey", "testValue").build()
    assertThat(tags.allTags().size).isEqualTo(1)
    assertThat(tags.allTags().get("testKey")).isEqualTo("testValue")
  }
}
