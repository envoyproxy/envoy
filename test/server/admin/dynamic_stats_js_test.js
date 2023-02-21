

function testDynamic(iframme) {
  return new Promise((resolve, reject) => {
    const pre = iframe.contentWindow.document.getElementById('dynamic-content-pre');
    assertTrue(pre, "pre not found");
    assertTrue(pre.textContent.indexOf('server.memory_allocated: ') != -1, "stat not found");
    resolve();
  });
}

addTest('/stats?format=dynamic', "testDynamic", testDynamic);
