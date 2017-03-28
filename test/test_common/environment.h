#pragma once

#include "common/json/json_loader.h"

class TestEnvironment {
public:
  /**
   * Obtain a private writable temporary directory.
   * @return const std::string& with the path to the temporary directory.
   */
  static const std::string& temporaryDirectory();

  /**
   * Prefix a given path with the private writable test temporary directory.
   * @param path path suffix.
   * @return std::string path qualified with temporary directory.
   */
  static std::string temporaryPath(const std::string& path) {
    return temporaryDirectory() + "/" + path;
  }

  /**
   * Obtain read-only test input data directory.
   * @return const std::string& with the path to the read-only test input directory.
   */
  static const std::string& runfilesDirectory();

  /**
   * Prefix a given path with the read-only test input data directory.
   * @param path path suffix.
   * @return std::string path qualified with read-only test input data directory.
   */
  static std::string runfilesPath(const std::string& path) {
    return runfilesDirectory() + "/" + path;
  }

  /**
   * Obtain pregenerated test ssl key/certificate directory.
   * @return std::string& with the path to the pregenerated test ssl key/certificate
   *         directory.
   */
  static std::string certsDirectory() { return runfilesPath("test/certs"); }

  /**
   * Prefix a given path with the pregenerated test ssl key/certificate directory.
   * @param path path suffix.
   * @return std::string path qualified with the pregenerated test ssl key/certificate directory.
   */
  static std::string certsPath(const std::string& path) { return certsDirectory() + "/" + path; }

  /**
   * Obtain Unix Domain Socket temporary directory.
   * @return std::string& with the path to the Unix Domain Socket temporary directory.
   */
  static const std::string unixDomainSocketDirectory();

  /**
   * Prefix a given path with the Unix Domain Socket temporary directory.
   * @param path path suffix.
   * @return std::string path qualified with the Unix Domain Socket temporary directory.
   */
  static std::string unixDomainSocketPath(const std::string& path) {
    return unixDomainSocketDirectory() + "/" + path;
  }

  /**
   * String environment path substitution.
   * @param str string with template patterns including {{ test_certs }}.
   * @return std::string with patterns replaced with environment values.
   */
  static std::string substitute(const std::string str);

  /**
   * Build JSON object from a string subject to environment path substitution.
   * @param json JSON with template patterns including {{ test_certs }}.
   * @return Json::ObjectPtr with built JSON object.
   */
  static Json::ObjectPtr jsonLoadFromString(const std::string& json);
};
