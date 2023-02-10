package io.envoyproxy.envoymobile

/**
 * Utility to enable admin interface.
 */
object EngineBuilderAdminUtil {
  /**
   * Enable admin interface on 127.0.0.1:9901 address. Admin interface is intended to be
   * used for development/debugging purposes only. Enabling it in production may open
   * your app to security vulnerabilities.
   *
   * Note this will not work with the default production build, as it builds with admin
   * functionality disabled via --define=admin_functionality=disabled
   *
   * @return this builder.
   */
  fun EngineBuilder.enableAdminInterface(): EngineBuilder {
    this.adminInterfaceEnabled = true
    return this
  }
}
