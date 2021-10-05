package io.envoyproxy.envoymobile.bazel

import io.github.classgraph.ClassGraph
import junit.framework.JUnit4TestAdapter
import junit.framework.TestSuite
import org.junit.runner.RunWith

@RunWith(org.junit.runners.AllTests::class)
object EnvoyMobileTestSuite {
  private val junitTestAnnotation = org.junit.Test::class.java.name

  @JvmStatic
  fun suite(): TestSuite {
    val suite = TestSuite()

    val scan = ClassGraph()
      .disableModuleScanning()
      .enableAnnotationInfo()
      .enableMethodInfo()
      .ignoreClassVisibility()
      .acceptPackages("io.envoyproxy", "test.kotlin.integration", "org.chromium.net")
      .scan()
    scan.getClassesWithMethodAnnotation(junitTestAnnotation)
      .asSequence()
      .sortedByDescending { it.name }
      .map { JUnit4TestAdapter(it.loadClass()) }
      .forEach(suite::addTest)

    return suite
  }
}
