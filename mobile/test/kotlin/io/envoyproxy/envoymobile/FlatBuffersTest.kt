package io.envoyproxy.envoymobile

import Test.Nested.SomeType
import com.google.flatbuffers.FlatBufferBuilder
import org.junit.Test

class FlatBuffersTest {

  @Test
  fun `flatbuffers construction`() {
    // This test doesn't really test any functionality, just demonstrates that we are able to utilize flatbuffers pending
    // real usage within the code base.
    val s = SomeType()
    val f = FlatBufferBuilder()
  }
}
