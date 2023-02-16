let current_stats = new Map;

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
  sorted_stats = [];
  let prev_stats = current_stats;
  current_stats = new Map();
  for (stat of data.stats) {
    let stat_record = prev_stats.get(stat.name);
    if (stat_record) {
      if (stat_record.value != stat.value) {
        stat_record.value = stat.value;
        ++stat_record.change_count;
      }
    } else {
      stat_record = {name: stat.name, value: stat.value, change_count: 0};
    }
    current_stats.set(stat.name, stat_record);
    sorted_stats.push(stat_record);
  }
  prev_stats = null;

  sorted_stats.sort((a, b) => {
    // Sort higher change-counts first.
    if (a.change_count != b.change_count) {
      return b.change_count - a.change_count;
    }

    // Secondarily put higher values first -- they are often more interesting.
    if (b.value != a.value) {
      return b.value - a.value;
    }

    // Fall back to forward alphabetic sort.
    if (a.name < b.name) {
      return -1;
    } else if (a.name > b.name) {
      return 1;
    }
    return 0;
  });

  let text = '';
  for (stat_record of sorted_stats) {
    text += stat_record.name + ': ' + stat_record.value + ' (' +
        stat_record.change_count + ')' + '\n';
  }
  content.textContent = text;

  // Update stats every 5 seconds.
  window.setTimeout(loadStats, 5000);
}

loadStats();
