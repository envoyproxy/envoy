package io.envoyproxy.envoymobile

import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ElementTest {

  @Test
  fun `creates new element as expected`() {
    // Should just work
    Element("foo")
  }

  @Test(expected = IllegalArgumentException::class)
  fun `throw exception when element name is rejected`() {
    // Should throw exception
    Element("foo9")
  }
}
