import search from './search_index.js';
const {raw_index}=search;

const excerpt_len_around = 100;

function search_text(needle) {
  console.time("search_text");

  const matches=[];
  for (const [doc_i, doc] of raw_index.documents.entries()){
    let start = 0;
    for (let _i =0; _i < doc.text.length; _i++){
      const idx = doc.text.slice(start).indexOf(needle);
      if (-1==idx) { break; }

      matches.push([doc_i | 0, start+idx]);
      start += idx + 1;
    }
  }
  console.timeEnd("search_text");

  const res = [];
  for (const [doc_i, idx] of matches) {
    const doc = raw_index.documents[doc_i];

    let title = undefined;
    for (const t of doc.titles){
      if (idx < t.offset) {
        break;
      }
      title = t;
    }

    const link = title ? doc.html_file_name + '#' + title.hash + '-' + title.content_html_id : doc.html_file_name;

    res.push({
      index: idx,
      title: title,
      link: link,
      document_index: doc_i,
    });
  }

  return res;
}

window.onload = function() {
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

    dom_pseudo_body.hidden = true;
    dom_search_matches.hidden = false;
    dom_search_matches.innerHTML = '';

    const matches = search_text(dom_input.value);

    const dom_search_title = window.document.createElement('h2');
    dom_search_title.innerText = `Search results (${matches.length}):`;
    dom_search_matches.append(dom_search_title);

    const match_entries = matches.entries();
    for (const [i, match] of match_entries) {
      const doc = raw_index.documents[match.document_index];
      const dom_match = window.document.createElement('p');

      const dom_doc = window.document.createElement('h3');
      dom_doc.innerHTML = `<a href="/blog/${doc.html_file_name}">${doc.title}</a>`
      if (match.title) {
        dom_doc.innerHTML += `: <a href="/blog/${match.link}">${match.title.title}</a>`;
      }
      dom_match.append(dom_doc);

      const dom_excerpt = window.document.createElement('p');
      const excerpt_idx_start = match.index - excerpt_len_around < 0 ? 0 : match.index - excerpt_len_around;
      const excerpt_idx_end = match.index + needle.length + excerpt_len_around;
      dom_excerpt.innerHTML = '...' + 
        doc.text.slice(excerpt_idx_start, match.index) +
        '<strong>' +
        doc.text.slice(match.index, match.index + needle.length) + 
        '</strong>' +
        doc.text.slice(match.index + needle.length, excerpt_idx_end) +
        '...';
      dom_match.append(dom_excerpt);

      if (i + 1 < matches.length){
        const dom_hr = window.document.createElement('hr');
        dom_match.append(dom_hr);
      }

      dom_search_matches.append(dom_match);
    }
  };
  dom_input.oninput = search_and_display_results;
  dom_input.onfocus = search_and_display_results;
};
