import search from './search_index.js';
const {raw_index}=search;

const excerpt_len_around = 100;

function search_text(needle) {
  console.time("search_text");

  const matches=[];
  for (let [doc_i, doc] of raw_index.documents.entries()){
    let start = 0;
    for (let _i =0; _i < doc.text.length; _i++){
      let idx = doc.text.slice(start).indexOf(needle);
      if (-1==idx) { break; }

      matches.push([doc_i | 0, start+idx]);
      start += idx + 1;
    }
  }
  console.timeEnd("search_text");

  const res = [];
  for (let [doc_i, idx] of matches) {
    const doc = raw_index.documents[doc_i];

    let title_i = 0;
    for (title_i of doc.title_text_offsets.keys()){
      const offset = doc.title_text_offsets[title_i];
      if (offset >= idx) {break;}
    }

    const title = doc.titles[title_i];
    const link = doc.html_file_name + '#' + title.hash + '-' + title.content_html_id;

    res.push({
      index: idx,
      title: title,
      link: link,
      document_index: doc_i,
    });
  }

  return res;
}

window.onload = function(){
  const dom_search_matches_wrapper = window.document.getElementById('search-matches-wrapper');
  const dom_search_matches = window.document.getElementById('search-matches');
  const dom_input = window.document.getElementById('search');
  const dom_pseudo_body = window.document.getElementById('pseudo-body');

  function search_and_display_results(event) {
    const needle = dom_input.value;
    if (needle.length < 3) {
      dom_pseudo_body.hidden = false;
      dom_search_matches_wrapper.hidden = true;
      return;
    }

    dom_pseudo_body.hidden = true;
    dom_search_matches_wrapper.hidden = false;
    dom_search_matches.innerHTML = '';

    const matches = search_text(dom_input.value);

    for (const match of matches.values()) {
      const doc = raw_index.documents[match.document_index];
      const dom_match = window.document.createElement('p');

      const dom_doc = window.document.createElement('h3');
      dom_doc.innerHTML = `<a href="/blog/${doc.html_file_name}">${doc.title}</a>`;
      dom_match.append(dom_doc);

      const dom_link = window.document.createElement('a');
      dom_link.setAttribute('href', '/blog/' + match.link);
      dom_link.innerText = match.title.title;
      dom_match.append(dom_link);

      const dom_excerpt = window.document.createElement('p');
      let excerpt_idx_start = match.index - excerpt_len_around < 0 ? 0 : match.index - excerpt_len_around;
      let excerpt_idx_end = match.index + needle.length + excerpt_len_around;
      dom_excerpt.innerHTML = '...' + 
        doc.text.slice(excerpt_idx_start, match.index) +
        '<strong>' +
        doc.text.slice(match.index, match.index + needle.length) + 
        '</strong>' +
        doc.text.slice(match.index + needle.length, excerpt_idx_end) +
        '...';
      dom_match.append(dom_excerpt);

      const dom_hr = window.document.createElement('hr');
      dom_match.append(dom_hr);

      dom_search_matches.append(dom_match);
    }
  };
  dom_input.oninput = search_and_display_results;
  dom_input.onfocus = search_and_display_results;
};
