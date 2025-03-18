import search from './search_index.js';
const {raw_index}=search;

function search_text(needle) {
  console.time("search_text");

  const matches=[];
  for (let [doc_i, doc] of raw_index.documents.entries()){
    let start = 0;
    for (let _i =0; _i < doc.text.length; _i++){
      let idx = doc.text.slice(start).indexOf(needle);
      console.log(start, idx);
      if (-1==idx) { break; }

      matches.push([doc_i | 0, start+idx]);
      start += idx + 1;
    }
  }
  console.timeEnd("search_text");

  console.log("[D010]", matches,matches.length);

  const res = [];
  for (let [doc_i, idx] of matches) {
    const doc = raw_index.documents[doc_i];

    let title_i = 0;
    for (title_i of doc.title_text_offsets.keys()){
      const offset = doc.title_text_offsets[title_i];
      if (offset >= idx) {break;}
    }

    const title = doc.titles[title_i];
    const link = doc.name + title
    const excerpt_len_around = 30;
    let excerpt_idx_start = idx - excerpt_len_around < 0 ? 0 : idx - excerpt_len_around;
    let excerpt_idx_end = idx + needle.length + excerpt_len_around;

    const excerpt = doc.text.slice(excerpt_idx_start, excerpt_idx_end).trim().replace('\n',' ');
    console.log("[D004]", title_i, doc.name, title, link, "`"+ excerpt + "`", idx - excerpt_len_around);

    res.push({
      title: title,
      link: link,
      excerpt: excerpt,
      document_name: doc.name,
    });
  }

  return res;
}

window.onload = function(){
  const search_results = document.getElementById('search-results');
  const input_elem = document.getElementById('search');
  function search_and_display_results(event){
    const needle = input_elem.value;
    if (needle.length < 3) {
      return;
    }

    search_results.innerHTML = '';

    const matches = search_text(input_elem.value);

    for (const match of matches.values()) {
      const elem = document.createElement('p');

      const document_name = document.createElement('span');
      document_name.innerText = match.document_name;
      elem.append(document_name);

      const excerpt = document.createElement('span');
      excerpt.innerText = match.excerpt;
      elem.append(excerpt);

      const link = document.createElement('a');
      link.setAttribute('href', '/blog/' + match.link);
      link.innerText = match.title;
      elem.append(link);

      search_results.prepend(elem);
    }
  };
  input_elem.oninput = search_and_display_results;
  // input_elem.onfocus = search_and_display_results;
};
