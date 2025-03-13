package io.envoyproxy.envoymobile

import com.google.common.truth.Truth.assertThat
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TagsBuilderTest {
  @Test
  fun `adds tags to tags`() {
    val tags = TagsBuilder().add("testKey", "testValue").build()
    assertThat(tags.allTags().size).isEqualTo(1)
    assertThat(tags.allTags().get("testKey")).isEqualTo("testValue")
  }

  @Test
  fun `puts a map of tags to tags`() {
    val tagsBuilder = TagsBuilder()
    val tagMap = mutableMapOf("testKey1" to "testValue1", "testKey2" to "testValue2")
    val tags = tagsBuilder.putAll(tagMap).build()
    assertThat(tags.allTags().size).isEqualTo(2)
    assertThat(tags.allTags().get("testKey1")).isEqualTo("testValue1")
    assertThat(tags.allTags().get("testKey2")).isEqualTo("testValue2")
  }
}
