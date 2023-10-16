package io.envoyproxy.envoymobile

/** Utility to enable HTTP/3. */
object EngineBuilderHTTP3Util {
  /**
   * Specify whether to enable experimental HTTP/3 (QUIC) support. Note the actual protocol will be
   * negotiated with the upstream endpoint and so upstream support is still required for HTTP/3 to
   * be utilized.
   *
   * @param doEnableHttp3 whether to enable HTTP/3.
   * @return This builder.
   */
  fun EngineBuilder.enableHttp3(doEnableHttp3: Boolean): EngineBuilder {
    this.enableHttp3 = doEnableHttp3
    return this
  }
}
