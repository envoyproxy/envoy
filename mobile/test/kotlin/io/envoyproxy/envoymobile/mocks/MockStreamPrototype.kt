package io.envoyproxy.envoymobile.mocks

import io.envoyproxy.envoymobile.Stream
import io.envoyproxy.envoymobile.StreamPrototype

/**
 * Mock implementation of `StreamPrototype` which is used to produce `MockStream` instances.
 *
 * @param onStart Closure that will be called each time a new stream is started from the prototype.
 */
class MockStreamPrototype(private val onStart: ((stream: MockStream) -> Unit)?) :
  StreamPrototype(MockEnvoyEngine()) {
  override fun start(): Stream {
    val callbacks = createCallbacks()
    val stream = MockStream(MockEnvoyHTTPStream(callbacks, false))
    onStart?.invoke(stream)
    return stream
  }
}
