// This file contains some minimal helper functions to test the admin panel rendering,
// including server-generated HTML content and pages that include some JavaScript.
//
// This is a very minimalistic test framework.


/**
 * Master list of tests. Each test in a struct with a url, name, and test Function.
 */
const test_list = [];


/**
 * Adds a new test to the test-list. These will be run after all test scripts
 * load, from admin.html's call to runAllTests().
 */
function addTest(url, name, test_function) {
  test_list.push({url: url, name: name, test_function: test_function});
}


/**
 * Renders a URL, and after 3 seconds delay, runs the 'tester' function.
 * Log whether that function failed (threw exception) or passed. Either
 * way, when that completes resolve a returned promise so the next test can
 * run.
 */
function runTest(url, name, tester) {
  const results = document.getElementById('test-results');
  results.textContent += name + ' ...';
  iframe = document.createElement("iframe");
  iframe.width = 800;
  iframe.height = 1000;
  iframe.src = url;
  document.body.appendChild(iframe);
  return new Promise((resolve, reject) => {
    window.setTimeout(() => {
      try {
        tester(iframe);
        results.textContent += 'passed\n';
      } catch (err) {
        results.textContent += 'FAILED: ' + err + '\n';
      }
      iframe.parentElement.removeChild(iframe);
      resolve(null);
    },
                      3000);
  });
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
 * Runs all tests added via addTest() above.
 */
function runAllTests() {
  addEventListener("DOMContentLoaded", () => {
    const next_test = (test_index) => {
      if (test_index < test_list.length) {
        test = test_list[test_index];
        runTest(test.url, test.name, test.test_function).then(() => {
          next_test(test_index + 1);
        });
      }
    };
    next_test(0);
  });
}
