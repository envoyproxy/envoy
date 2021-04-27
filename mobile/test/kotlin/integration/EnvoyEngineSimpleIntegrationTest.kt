package test.kotlin.integration

import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.engine.JniLibrary
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class EnvoyEngineSimpleIntegrationTest {

  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `ensure engine build and termination succeeds with no errors`() {
    val engine = EngineBuilder().build()
    Thread.sleep(5000)
    engine.terminate()
    assertThat(true).isTrue()
  }
}
