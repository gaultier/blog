
const excerpt_len_around = 100;

function search_text(needle) {
  needle = needle.toLowerCase();
  console.time("search_text");

  const file_scores = new Map();

  for (let i = 0; i <= needle.length - 3; i++) {
    const trigram = needle[i] + needle[i+1] + needle[i+2];

    const match = window.search_index.trigram_to_file_idx[trigram];
    if (match === undefined) {
      continue;
    }

    const docs_with_this_trigram = Object.entries(match).length;

    for (const [file, count] of Object.entries(match)) {
      const score = file_scores.get(file) || 0;
      file_scores.set(file, score + count * (1/docs_with_this_trigram) );
    }
  }
  
  console.timeEnd("search_text");
  return file_scores;
}

window.onload = function() {
  fetch('/blog/search_index.json')
    .then(r => r.json())
    .then(j => {
      j.idx_to_file = new Map();

      for (const [file, idx] of Object.entries(j.file_to_idx)) {
        j.idx_to_file.set(idx, file);
      }
    window.search_index = j;
  });

  const dom_search_matches = window.document.getElementById('search-matches');
  const dom_input = window.document.getElementById('search');
  const dom_pseudo_body = window.document.getElementById('pseudo-body');

  function search_and_display_results(_event) {
    const needle = dom_input.value;
    if (needle.length < 3) {
      dom_pseudo_body.hidden = false;
      dom_search_matches.hidden = true;
      return;
    }

    dom_pseudo_body.hidden = false;
    dom_search_matches.hidden = false;
    dom_search_matches.innerHTML = '';

    const scores = search_text(dom_input.value);

    dom_pseudo_body.innerHTML = '<h3>Search results</h3><ul>' ;

    let search_results = [...scores.entries()];
    search_results.sort((a,b) => b[1] - a[1]);
    for (const [file_idx, score] of search_results) {
      const file = search_index.idx_to_file.get(Number(file_idx));

      dom_pseudo_body.innerHTML += `<li> <a href="${file}">${file}: ${score}</a></li>`
    }
    dom_pseudo_body.innerHTML += '</ul>' ;

  };
  dom_input.oninput = search_and_display_results;
  dom_input.onfocus = search_and_display_results;
};
