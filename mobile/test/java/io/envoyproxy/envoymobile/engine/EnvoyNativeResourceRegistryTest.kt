package io.envoyproxy.envoymobile.engine

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import com.google.common.truth.Truth.assertThat
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class EnvoyNativeResourceRegistryTest {
  @Test
  fun `release callbacks are invoked when EnvoyNativeResourceWrappers are flagged as unreachable`() {
    val latch = CountDownLatch(1)
    val testHandle: Long = 77
    // Force Wrapper out of scope so the GC picks it up. ¯\_(ツ)_/¯
    for (i in 0..1 step 2) {
      var testResourceWrapper: EnvoyNativeResourceWrapper? = object : EnvoyNativeResourceWrapper {}
      val testResourceReleaser = object : EnvoyNativeResourceReleaser {
        override fun release(nativeHandle: Long) {
          assertThat(nativeHandle).isEqualTo(testHandle)
          latch.countDown()
        }
      }
      EnvoyNativeResourceRegistry.globalRegister(testResourceWrapper, testHandle, testResourceReleaser)
    }

    System.runFinalization()
    System.gc()
    assertThat(latch.await(2000, TimeUnit.MILLISECONDS)).isTrue()
  }

  @Test
  fun `release callbacks are not invoked when EnvoyNativeResourceWrappers remain reachable`() {
    val latch = CountDownLatch(1)
    val testHandle: Long = 77
    // Wrapper will remain reachable.
    val testResourceWrapper = object : EnvoyNativeResourceWrapper {}
    for (i in 0..1 step 2) {
      val testResourceReleaser = object : EnvoyNativeResourceReleaser {
        override fun release(nativeHandle: Long) {
          // Should be not called.
          latch.countDown()
        }
      }
      EnvoyNativeResourceRegistry.globalRegister(testResourceWrapper, testHandle, testResourceReleaser)
    }

    System.runFinalization()
    System.gc()
    assertThat(latch.await(2000, TimeUnit.MILLISECONDS)).isFalse()
  }
}
