package io.envoyproxy.envoymobile.mocks

import io.envoyproxy.envoymobile.Stream
import io.envoyproxy.envoymobile.StreamPrototype
import java.util.concurrent.Executor

/**
 * Mock implementation of `StreamPrototype` which is used to produce `MockStream` instances.
 *
 * @param onStart Closure that will be called each time a new stream is started from the prototype.
 */
class MockStreamPrototype(private val onStart: ((stream: MockStream) -> Unit)?) :
  StreamPrototype(MockEnvoyEngine()) {
  override fun start(executor: Executor?): Stream {
    val callbacks = createCallbacks(executor)
    val stream = MockStream(MockEnvoyHTTPStream(callbacks, false))
    onStart?.invoke(stream)
    return stream
  }
}
