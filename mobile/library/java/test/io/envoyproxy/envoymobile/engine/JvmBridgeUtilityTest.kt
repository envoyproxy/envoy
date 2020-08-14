package io.envoyproxy.envoymobile.engine

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class JvmBridgeUtilityTest {

  @Test
  fun `retrieveHeaders produces a Map with all headers provided via passHeaders`() {
    val utility = JvmBridgeUtility()
    utility.passHeader("test-0".toByteArray(), "value-0".toByteArray(), true)
    utility.passHeader("test-1".toByteArray(), "value-1".toByteArray(), false)
    utility.passHeader("test-1".toByteArray(), "value-2".toByteArray(), false)

    val headers = utility.retrieveHeaders()
    val expectedHeaders = mapOf(
      "test-0" to listOf("value-0"),
      "test-1" to listOf("value-1", "value-2")
    )

    assertThat(headers)
      .hasSize(2) // Two keys / header name
      .usingRecursiveComparison().isEqualTo(expectedHeaders)
  }

  @Test
  fun `validateCount checks if the expected number of header values in the map matches the actual`() {
    val utility = JvmBridgeUtility()
    assertThat(utility.validateCount(1)).isFalse()

    utility.passHeader("test-0".toByteArray(), "value-0".toByteArray(), true)
    assertThat(utility.validateCount(1)).isTrue()

    utility.passHeader("test-1".toByteArray(), "value-1".toByteArray(), false)
    assertThat(utility.validateCount(2)).isTrue()

    utility.passHeader("test-1".toByteArray(), "value-2".toByteArray(), false)
    assertThat(utility.validateCount(3)).isTrue()

    assertThat(utility.validateCount(4)).isFalse()
  }

  @Test
  fun `retrieveHeaders resets internal state`() {
    val utility = JvmBridgeUtility()
    utility.passHeader("test-0".toByteArray(), "value-0".toByteArray(), true)
    utility.passHeader("test-1".toByteArray(), "value-1".toByteArray(), false)
    utility.passHeader("test-1".toByteArray(), "value-2".toByteArray(), false)
    assertThat(utility.validateCount(3)).isTrue()

    val headers = utility.retrieveHeaders()
    assertThat(utility.validateCount(0)).isTrue()

    utility.passHeader("test-2".toByteArray(), "value-3".toByteArray(), true)

    val nextHeaders = utility.retrieveHeaders()
    val expectedHeaders = mapOf(
      "test-2" to listOf("value-3")
    )

    assertThat(nextHeaders)
      .hasSize(1) // One key / header name
      .usingRecursiveComparison().isEqualTo(expectedHeaders)
  }

  @Test(expected = AssertionError::class)
  fun `starting a new header block before a previous one is finished is an error`() {
    val utility = JvmBridgeUtility()

    utility.passHeader("test-0".toByteArray(), "value-0".toByteArray(), true)
    utility.passHeader("test-1".toByteArray(), "value-1".toByteArray(), true)
  }

  @Test(expected = AssertionError::class)
  fun `not starting a new header block is an error`() {
    val utility = JvmBridgeUtility()

    utility.passHeader("test-0".toByteArray(), "value-0".toByteArray(), false)
  }
}
