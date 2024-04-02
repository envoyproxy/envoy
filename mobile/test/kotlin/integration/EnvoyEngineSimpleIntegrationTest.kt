package test.kotlin.integration

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.JniLibrary
import org.junit.Test

class EnvoyEngineSimpleIntegrationTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `ensure engine build and termination succeeds with no errors`() {
    val engine =
      EngineBuilder().addLogLevel(LogLevel.DEBUG).setLogger { _, msg -> print(msg) }.build()
    Thread.sleep(5000)
    engine.terminate()
    assertThat(true).isTrue()
  }
}
