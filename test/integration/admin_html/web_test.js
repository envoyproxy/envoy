// This file contains some minimal helper functions to test the admin panel rendering,
// including server-generated HTML content and pages that include some JavaScript.
//
// The unit test style is similar to that supported by Google's Closure
// Compiler, but without taking on a large dependency or build infrastructure.
//
// Linted with https://validatejavascript.com/, 'Google' settings, long-line
// checking disabled, indent-checking disabled.
//
// See source/server/admin/javascript.md for background info.


/**
 * List of all tests. Each test in a struct with a url, name, and test Function.
 *
 * @array{Object}
 */
const testList = [];

/**
 * Adds a new test to the test-list. These will be run after all test scripts
 * load, from admin.html's call to runAllTests().
 *
 * TODO(jmarantz): rename to runAsyncTest.
 *
 * @param {string} url
 * @param {string} name
 * @param {function(!HTMLElement): Promise} testFunction
 * exported addTest
 */
function addTest(url, name, testFunction) { // eslint-disable-line no-unused-vars
  testList.push({'url': url, 'name': name, 'testFunction': testFunction});
}

/**
 * Adds a new test to the test-list. These will be run directly. A test
 * passes if it doesn't throw.
 *
 * @param {string} name
 * @param {function()} testFunction
 * exported addTest
 */
function addBlockingTest(name, testFunction) { // eslint-disable-line no-unused-vars
  testList.push({'name': name, 'testFunction': testFunction});
}

/**
 * Provides an async version of the `onload` event.
 *
 * @param {!HTMLElement} iframe
 * @return {!Promise}
 */
function waitForOnload(iframe) {
  return new Promise((resolve, reject) => {
    iframe.onload = () => {
      resolve();
    };
  });
}

function makeFrame(type) {
  const frame = document.createElement(type);
  frame.width = 800;
  frame.height = 1000;
  document.body.appendChild(frame);
  return frame;
}

/**
 * Renders a URL, and after 3 seconds delay, runs the 'tester' function.
 * Log whether that function failed (threw exception) or passed. Either
 * way, when that completes resolve a returned promise so the next test can
 * run.
 *
 * @param {string} url
 * @param {string} name
 * @param {function(!HTMLElement): !Promise} tester
 * @return {!Promise}
 */
async function runAsyncTest(url, name, tester) {
  const results = document.getElementById('test-results');
  results.textContent += name + ' ...';
  const iframe = makeFrame('iframe');
  iframe.src = url;
  await waitForOnload(iframe);
  try {
    await tester(iframe);
    results.textContent += 'passed\n';
  } catch (err) {
    results.textContent += 'FAILED: ' + err + '\n';
  }
  iframe.parentElement.removeChild(iframe);
}


/**
 * @param {string} name
 * @param {function(!HTMLElement)} tester
 */
/*async function runBlockingTest(url, name, tester) {
  const results = document.getElementById('test-results');
  results.textContent += name + ' ...';
  const iframe = makeFrame('iframe');
  iframe.src = url;
  await waitForOnload(iframe);
  try {
    await tester(div);
    results.textContent += 'passed\n';
    div.parentElement.removeChild(div);
  } catch (err) {
    results.textContent += 'FAILED: ' + err + '\n';
  }
}*/

/**
 * Checks for a condition, throwing an exception with a comment if it fails.
 *
 * @param {Object} cond
 * @param {string} comment
 */
function assertTrue(cond, comment) {
  if (!cond) {
    throw new Error(comment);
  }
}


/**
 * Checks for equality, throwing an exception with a comment if it fails.
 *
 * @param {Object} expected
 * @param {Object} actual
 */
function assertEq(expected, actual) { // eslint-disable-line no-unused-vars
  assertTrue(expected == actual, 'assertEq mismatch: expected ' + expected + ' got ' + actual);
}

function assertLt(a, b) { // eslint-disable-line no-unused-vars
  assertTrue(a < b, 'assertLt mismatch: ' + a + ' < ' + b);
}

async function nextTest(index) {
  if (index < testList.length) {
    test = testList[index];
    ++index;
    const name_index = test.name + '(' + index + '/' + testList.length + ')';
    if (test.url) {
      await runAsyncTest(test.url, name_index, test.testFunction).then(() => { nextTest(index); });
    } else {
      runBlockingTest(name_index, test.testFunction);
      nextTest(index);
    }
  }
}


/**
 * Async version of windows.setTimeout.
 */
function asyncTimeout(ms) {
  return new Promise(resolve => window.setTimeout(resolve, ms));
}


/**
 * Runs all tests added via addTest() above.
 */
async function runAllTests() {
  nextTest(0);
}

// Trigger the tests once all JS is loaded.
window.addEventListener('DOMContentLoaded', runAllTests);
