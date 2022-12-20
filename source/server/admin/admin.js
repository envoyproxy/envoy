/* jshint esversion: 6 */

// Entry-point into the javascript/ajax populated outline-UI for stats.
function populateScopes(json) {
  populateScope(document.getElementById('scopes-outline'), json.stats, json.scopes, '');
}

// Fetches a Stats::Scope from the server and then populates it into the
// UI. Note that the stat leaves may have lots of hierarchy in their names,
// which we will use to populate synthetic scopes rather than providing a
// flattened view of the entire scope.
function fetchScope(parent, value_record) {
  let url = 'stats?scope=' + value_record.name + '&show_json_scopes&format=json';
  url += '&type=' + document.getElementById('type').value;
  if (document.getElementById('usedonly').checked) {
    url += '&usedonly';
  }
  if (document.getElementById('filter').value) {
    url += '&filter=' + document.getElementById('filter').value;
  }
  fetch(url).then((response) => response.json())
      .then(json => populateScope(parent,
                                  value_record.stats.concat(json.stats),
                                  value_record.scopes.concat(json.scopes),
                                  value_record.name));
}

class ValueRecord {
  constructor(names, name) {
    this.name = name;
    const split = name.split('.');
    this.label = split[split.length - 1];
    this.fetch = false;
    this.value = null;
    // this.stats = [];
    this.scopes = [];
    names.set(name, this);
  }

  isScope() {
    return this.fetch /* || this.stats.length > 0 */;
  }

  labelValue() {
    if (this.value == null) {
      return this.label;
    }
    return this.label + ": " + this.value;
  }
}

// Having discovered a hierarchical stat name within a scope, we are going to
// make a synthetic scope in the UI, saving all the values we'll need inside
// using the same JSON format sent by the server. When the user expands into
// this synthetic hierarchy we can just populate the UI directly from the saved
// values.
/*function addNameToSyntheticScope(synthetic_scope, name, value, names) {
  let value_record = names.get(synthetic_scope);
  if (!value_record) {
    value_record = new ValueRecord(names, synthetic_scope);
  }
  value_record.stats.push({name: name, value: value});
}*/

// Adds a scope the the UI, underneath the specified parent HTML element.
// expand_fn is called when the user clicks the button to expand the scope.
// This enables a common UI to be used whether the scope is a Stats::Scope in
// the server, or simply name-based hierarchy that is discovered in JavaScript.
function renderScope(value_record) {
  const li_tag = document.createElement('li');
  const span_tag = document.createElement('span');
  span_tag.setAttribute('title', value_record.name);
  span_tag.setAttribute('class', 'caret');
  span_tag.textContent = value_record.labelValue();
  const children_tag = document.createElement('ul');
  children_tag.setAttribute('class', 'scope-children');
  li_tag.appendChild(span_tag);
  li_tag.appendChild(children_tag);

  span_tag.addEventListener("click", () => {
    span_tag.classList.toggle("caret-down");
    if (children_tag.firstChild) {
      while (children_tag.firstChild) {
        children_tag.removeChild(children_tag.firstChild);
      }
    } else {
      if (value_record.fetch) {
        fetchScope(children_tag, value_record);
      } else {
        populateScope(children_tag, value_record.stats, value_record.scopes,
                      value_record.name);
      }
    }
  });
  return li_tag;
}

//function addServerScope(parent, sub_scope, prefix, scopes) {
//  scopes.set(sub_scope, { prefix: prefix, fetch: true });
//}

// Populates a scope into the parent, based on the specified json,
// which as the format:
//    {stats: [{"name": "name1", "value": value1},
//             {"name": "name2", "value": value2}],
//     scopes: ["scope1", "scope2, "scope3"]}
function populateScope(parent, stats, scopes, prefix) {
  // First we collect all the server-scopes, synthetic-scopes and stats, so we
  // sort them together. We'll also keep a map so we don't duplicate. It's possible
  // in princple for someone to create with counter "c1" in scope "foo.bar", and
  // counter "bar.c2" in scope "foo", in which case "foo.bar" will be populated
  // both as a server-scope *and* a synthetic scope. So our map value representation
  // needs for us to make "c1" and "c2" both be children of the node "foo.bar".
  //
  // It's also, I think, possible to have a scope "foo.bar" and a counter "foo.bar".
  const names = new Map();

  for (let sub_scope of scopes) {
    //addServerScope(parent, sub_scope, prefix, scopes);
    let value_record = new ValueRecord(names, sub_scope); //, sub_scope /*prefix*/);
    value_record.fetch = true;
    names.set(sub_scope, value_record);
  }

  for (let stat of stats) {
    processStat(parent, stat, prefix, names);
  }

  // Now we've collected all the stats at the current level, and all the synthetic
  // and server-defined scopeds, into one map. We'll sort the map and render.
  let keys = Array.from(names.keys());
  keys.sort();

  for (let name of keys) {
    const value_record = names.get(name);
    if (value_record.isScope()) {
      parent.appendChild(renderScope(value_record));
    } else {
      const li_tag = document.createElement('li');
      li_tag.textContent = value_record.labelValue();
      li_tag.setAttribute('title', value_record.name);
      parent.appendChild(li_tag);
    }
  }
}

// Process a stat from a scope, constructing a value-string for histograms.
function processStat(parent, stat, prefix, names) {
  if (stat.histograms && stat.histograms.computed_quantiles) {
    for (let histogram of stat.histograms.computed_quantiles) {
      let val_strs = [];
      for (let value of histogram.values) {
        if (value.interval || value.cumulative) {
          val_strs.push('(' + value.interval + ',' + value.cumulative + ')');
        }
      }
      const val_str = (val_strs.length == 0) ? 'no values' : ('[' + val_strs.join(',') + ']');
      addStat(parent, histogram.name, val_str, prefix, names);
    }
  } else {
    addStat(parent, stat.name, stat.value, prefix, names);
  }
}

// Adds a stat to the UI into the parent HTML element. If it looks liek
// the stat has some extra hierarchy in it, then synthesize that hierarchy
// into an expandable scope in the outline, collecting all the stats we'll
// need it to include.
function addStat(parent, name, value, prefix, names) {
  const full_name = name;
  if (prefix) {
    const prefix_dot = prefix + '.';
    if (!name.startsWith(prefix_dot)) {
      console.log('addStat: ' + name + ' does not start with ' + prefix_dot);
    }
    name = name.substring(prefix_dot.length);
  }

  // If the name has hierarchy in it, then create a scope node instead.
  const split = name.split('.');
  if (split.length >= 2) {
    alert('expecting only one dot in ' + split);
    //const synthetic_scope = prefix ? (prefix + '.' + split[0]) : split[0];
    //addNameToSyntheticScope(synthetic_scope, full_name, value, names);
  } else {
    let value_record = names.get(full_name);
    if (value_record == null) {
      value_record = new ValueRecord(names, full_name);
    }
    value_record.value = value;
  }
}
