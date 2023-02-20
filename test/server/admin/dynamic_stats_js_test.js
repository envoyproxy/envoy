function testPage(url, tester) {
  //fetch(url).then((response) => response.text()).then((text) => tester(text));
  iframe = document.createElement("iframe");
  iframe.width = 800;
  iframe.height = 1000;
  iframe.src = url;
  document.body.appendChild(iframe);
  window.setTimeout(() => tester(iframe), 3000);
}

function assertTrue(cond, comment) {
  if (!cond) {
    throw new Error(comment);
  }
}

function testDynamic(iframe) {
  document.getElementById('test-results').textContent += 'Running testDynamic ...';
  try {
    const pre = iframe.contentWindow.document.getElementById('dynamic-content-pre');
    assertTrue(pre);
    assertTrue(pre.textContent.indexOf('server.memory_allocated: ') != -1);
    document.getElementById('test-results').textContent += 'passed\n';
  } catch (err) {
    document.getElementById('test-results').textContent += 'FAILED: ' + err + '\n';
  }
  iframe.parentElement.removeChild(iframe);
}

function runTests() {
  testPage('/stats?format=dynamic', testDynamic);
}

addEventListener("DOMContentLoaded", runTests);
