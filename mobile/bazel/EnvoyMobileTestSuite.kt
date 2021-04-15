package io.envoyproxy.envoymobile.bazel

import java.io.File
import java.net.URLClassLoader
import java.util.zip.ZipFile
import junit.framework.JUnit4TestAdapter
import junit.framework.TestSuite
import org.junit.runner.RunWith

/**
 * This is class is taken from https://stackoverflow.com/questions/46365464/how-to-run-all-tests-in-bazel-from-a-single-java-test-rule
 *
 * Translated to Kotlin and slightly modified
 *
 * Requirements:
 * 1. This only allows tests to be run if the package is within `io.envoyproxy.envoymobile`
 * 2. This requires at least one test to be defined in the test target
 */
@RunWith(org.junit.runners.AllTests::class)
object EnvoyMobileTestSuite {
  private const val TEST_SUFFIX = "Test"
  private const val CLASS_SUFFIX = ".class"

  @JvmStatic
  fun suite(): TestSuite {
    val suite = TestSuite()
    val classLoader = Thread.currentThread().contextClassLoader as URLClassLoader
    val testAdapters = mutableListOf<JUnit4TestAdapter>()
    // The first entry on the classpath contains the srcs from java_test
    val classesInJar = findClassesInJar(File(classLoader.urLs[0].path))
    for (clazz in classesInJar) {
      val name = Class.forName(clazz)
      val testAdapter = JUnit4TestAdapter(name)
      testAdapters.add(testAdapter)
    }

    if (testAdapters.isEmpty()) {
      throw NoTestFoundException("Unable to find any tests in test target")
    }

    for (testAdapter in testAdapters) {
      suite.addTest(testAdapter)
    }
    return suite
  }

  private fun findClassesInJar(jarFile: File): Set<String> {
    val classNames = mutableSetOf<String>()

    ZipFile(jarFile).use { zipFile ->
      val entries = zipFile.entries()
      for (entry in entries) {
        val entryName = entry.name

        if (entryName.endsWith(CLASS_SUFFIX)) {
          val classNameEnd = entryName.length - CLASS_SUFFIX.length
          val resolvedClass = entryName.substring(0, classNameEnd).replace('/', '.')
          if (resolvedClass.endsWith(TEST_SUFFIX)) {
            classNames.add(resolvedClass)
          }
        }
      }
    }
    return classNames
  }
}

class NoTestFoundException(message: String) : RuntimeException(message)
