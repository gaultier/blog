
const excerpt_len_around = 100;

function search_text(needle) {
  needle = needle.toLowerCase();
  console.time("search_text");

  const file_scores = new Map();

  for (let i = 0; i <= needle.length - 3; i++) {
    const trigram = needle[i] + needle[i+1] + needle[i+2];

    const match = window.search_index.trigram_to_file_idx[trigram];
    console.log('trigram: ', trigram, match);
    if (match === undefined) {
      continue;
    }

    for (let file of match) {
      const score = file_scores.get(file);
      console.log("match: ", file, match, score);
      if (score == undefined) {
        file_scores.set(file, 1);
      } else {
        file_scores.set(file, score + 1);
      }
    }
  }
  
  console.timeEnd("search_text");
  return file_scores;
}

window.onload = function() {
  fetch('/blog/search_index.json')
    .then(r => r.json())
    .then(j => {
      console.log('loaded search index', j); 
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
    search_results.sort((a,b) => b[0] - a[0]);
    for (const [file_idx, score] of scores.entries()) {
      const file = search_index.idx_to_file.get(file_idx);

      dom_pseudo_body.innerHTML += `<li> <a href="${file}">${file}: ${score}</a></li>`
    }
    dom_pseudo_body.innerHTML += '</ul>' ;

  };
  dom_input.oninput = search_and_display_results;
  dom_input.onfocus = search_and_display_results;
};
