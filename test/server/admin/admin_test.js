// This file contains some minimal helper functions to test the admin panel rendering,
// including server-generated HTML content and pages that include some JavaScript.
//
// This is a very minimalistic test framework.


/**
 * Renders a URL, and after 3 seconds delay, runs the 'tester' function.
 */
function testPage(url, name, tester) {
  document.getElementById('test-results').textContent += name + ' ...';
  iframe = document.createElement("iframe");
  iframe.width = 800;
  iframe.height = 1000;
  iframe.src = url;
  document.body.appendChild(iframe);
  window.setTimeout(() => tester(iframe), 3000);
}
