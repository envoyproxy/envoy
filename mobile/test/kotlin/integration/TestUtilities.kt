package test.kotlin.integration

import io.envoyproxy.envoymobile.Engine
import java.time.Instant
import java.util.concurrent.TimeUnit
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import kotlin.time.DurationUnit
import org.junit.Assert.fail

/** Gets the stats in the form of `Map<String, String>`. */
fun Engine.getStats(): Map<String, String> {
  // `dumpStats()` produces the following format:
  // key1: value1
  // key2: value2
  // key3: value3
  // ...
  val lines = dumpStats().split("\n")
  return lines
    .mapNotNull {
      val keyValue = it.split(": ")
      if (keyValue.size == 2) {
        Pair(keyValue[0], keyValue[1])
      } else {
        null
      }
    }
    .toMap()
}

/**
 * Waits for 5 seconds (default) until the stat of the given [name] is greater than equal the
 * specified [expectedValue].
 *
 * @throws java.lang.AssertionError throw when the the operation timed out waiting for the stat
 *   value to be greater than the [expectedValue].
 */
fun Engine.waitForStatGe(
  name: String,
  expectedValue: Long,
  timeout: Duration = 5.seconds,
) {
  val waitTime = Instant.now().plusMillis(timeout.toLong(DurationUnit.MILLISECONDS))
  var currentValue = getStats()[name]
  while (currentValue == null || currentValue.toLong() < expectedValue) {
    if (Instant.now() > waitTime) {
      fail("Timed out waiting for $name to be greater than equal $expectedValue")
    }
    TimeUnit.MILLISECONDS.sleep(10)
    currentValue = getStats()[name]
  }
}
