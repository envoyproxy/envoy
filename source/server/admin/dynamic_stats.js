// Before first checkin:
//   * top-n algo to limit compute overhead with high cardinality stats with user control of N.
//   * alternate sorting criteria, reverse-sort controls, etc.

// Follow-ups:
//   * render histograms
//   * detect when user is not looking at page and stop or slow down pinging the server
//   * hieararchical display
//   * json flavor to send hierarchical names to save serialization/deserialization costs

let current_stats = new Map;

const param_id_prefix = 'param-1-stats-';

function loadStats() {
  val = name => name + '=' + document.getElementById(param_id_prefix + name).value;
  const params = ['usedonly', 'filter', 'type', 'histogram_buckets'];
  const url = '/stats?format=json&' + params.map(val).join('&');
  fetch(url, {
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
  const max_elt = document.getElementById('dynamic-max-display-count');
  const max_value = max_elt.value;
  const max = parseInt(max_value) || 50;
  let index = 0;
  for (stat_record of sorted_stats) {
    if (++index == max) {
      break;
    }
    text += stat_record.name + ': ' + stat_record.value + ' (' +
        stat_record.change_count + ')' + '\n';
  }
  content.textContent = text;

  // Update stats every 5 seconds.
  window.setTimeout(loadStats, 5000);
}

addEventListener('DOMContentLoaded', loadStats);
