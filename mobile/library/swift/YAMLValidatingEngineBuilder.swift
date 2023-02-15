/// An engine builder that validates the YAML configuration and asserts on failure.
/// Not part of the stable public API.
///
/// - returns: The `EngineBuilder`.
@_spi(YAMLValidation)
public func YAMLValidatingEngineBuilder() -> EngineBuilder {
  EngineBuilder()
    .setExperimentalValidateYAMLCallback { success in
      if success {
        print("YAML comparison succeeded")
      } else {
        assertionFailure("YAML comparison failed")
      }
    }
}
