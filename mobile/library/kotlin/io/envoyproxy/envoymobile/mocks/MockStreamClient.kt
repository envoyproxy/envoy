package io.envoyproxy.envoymobile

/**
 * Mock implementation of `StreamClient` which produces `MockStreamPrototype` values.
 *
 * @param onStartStream Closure that may be set to observe the creation of new streams. It will be
 *   called each time `newStreamPrototype()` is executed. Typically, this is used to capture streams
 *   on creation before sending values through them.
 */
class MockStreamClient(var onStartStream: ((MockStream) -> Unit)?) : StreamClient {
  override fun newStreamPrototype(): StreamPrototype {
    return MockStreamPrototype { onStartStream?.invoke(it) }
  }
}
