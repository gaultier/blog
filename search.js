import search from './search_index.js';
const {raw_index}=search;

function search_text(needle) {
  console.time("search_text");

  const matches=[];
  for (let [doc_i, doc] in raw_index.documents.entries()){
    const idx = doc.text.indexOf(needle);
    if (-1==idx) { continue; }
    matches.push([doc_i | 0,idx]);
  }
  console.timeEnd("search_text");

  console.log("[D010]", matches);

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
    console.log("[D004]", pos, title_i, doc.name, title, link, "`"+ excerpt + "`", idx - excerpt_len_around);

    res.push({
      title: title,
      link: link,
      excerpt: excerpt,
      document_name: doc.name,
    });
  }

  return res;
}
