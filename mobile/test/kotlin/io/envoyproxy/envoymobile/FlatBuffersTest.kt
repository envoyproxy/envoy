package io.envoyproxy.envoymobile

import Test.Nested.SomeType
import com.google.flatbuffers.FlatBufferBuilder
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class FlatBuffersTest {

  @Test
  @OptIn(ExperimentalUnsignedTypes::class)
  fun `flatbuffers construction`() {
    // This test doesn't really test any functionality, just demonstrates that we are able to utilize flatbuffers pending
    // real usage within the code base.
    assertThat(SomeType()).isNotNull()
    assertThat(FlatBufferBuilder()).isNotNull()
  }
}
