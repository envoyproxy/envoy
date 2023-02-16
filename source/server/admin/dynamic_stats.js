function loadStats() {
  fetch('/stats?usedonly=on&filter=&format=json&type=All&histogram_buckets=cumulative', {
    method: 'GET',
    headers: {},
  })
      .then(response => response.json())
      .then(data => renderStats(data));
}

function renderStats(data) {
  const content = document.getElementById('dynamic-stats');
  while (content.firstChild) {
    content.removeChild(content.firstChild);
  }
  stats = [];
  for (stat of data.stats) {
    stats.push(stat.name + ': ' + stat.value);
  }
  content.textContent = stats.join('\n');
}

loadStats();
