package org.chromium.net.testing;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * The java instrumentation tests are normally fairly large (in terms of
 * dependencies), and the test suite ends up containing a large amount of
 * tests that are not trivial to filter / group just by their names.
 * Instead, we use this annotation: each test should be annotated as:
 *     @Feature({"Foo", "Bar"})
 * in order for the test runner scripts to be able to filter and group
 * them accordingly (for instance, this enable us to run all tests that exercise
 * feature Foo).
 */
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface Feature {
  /**
   * @return A list of feature names.
   */
  String[] value();
}
