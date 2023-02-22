// This file contains some minimal helper functions to test the admin panel rendering,
// including server-generated HTML content and pages that include some JavaScript.
//
// The unit test style is similar to that supported by Google's Closure
// Compiler, but without taking on a large dependency or build infrastructure.
//
// Linted with https://validatejavascript.com/, 'Google' settings, long-line
// checking disabled, indent-checking disabled.


/**
 * List of all tests. Each test in a struct with a url, name, and test Function.
 *
 * @array{Object}
 */
const test_list = [];


/**
 * Adds a new test to the test-list. These will be run after all test scripts
 * load, from admin.html's call to runAllTests().
 */
function addTest(url, name, test_function) {
  test_list.push({"url": url, "name": name, "test_function": test_function});
}


/**
 * Provides an async version of the onload event.
 */
function waitForOnload(iframe) {
  return new Promise((resolve, reject) => {
    iframe.onload = () => {
      resolve();
    };
  });
}

/**
 * Renders a URL, and after 3 seconds delay, runs the 'tester' function.
 * Log whether that function failed (threw exception) or passed. Either
 * way, when that completes resolve a returned promise so the next test can
 * run.
 */
async function runTest(url, name, tester) {
  const results = document.getElementById("test-results");
  results.textContent += name + " ...";
  const iframe = document.createElement("iframe");
  iframe.width = 800;
  iframe.height = 1000;
  iframe.src = url;
  document.body.appendChild(iframe);
  await waitForOnload(iframe);
  try {
    await tester(iframe);
    results.textContent += "passed\n";
  } catch (err) {
    results.textContent += "FAILED: " + err + "\n";
  }
  iframe.parentElement.removeChild(iframe);
}


/**
 * Checks for a condition, throwing an exception with a comment if it fails.
 */
function assertTrue(cond, comment) {
  if (!cond) {
    throw new Error(comment);
  }
}


/**
 * Checks for equality, throwing an exception with a comment if it fails.
 */
function assertEq(expected, actual) {
  assertTrue(expected == actual, "assertEq mismatch: expected " + expected + " got " + actual);
}


/**
 * Runs all tests added via addTest() above.
 */
function runAllTests() {
  const next_test = (test_index) => {
    if (test_index < test_list.length) {
      test = test_list[test_index];
      ++test_index;
      runTest(test.url, test.name + "(" + test_index + "/" + test_list.length + ")",
              test.test_function).then(() => {
                next_test(test_index);
              });
    }
  };
  next_test(0);
}

// Trigger the tests once all JS is loaded.
window.addEventListener("DOMContentLoaded", runAllTests);
