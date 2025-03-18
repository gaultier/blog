import search from './search_index.js';
const {raw_index}=search;

console.time("search_text_without_index");
const needle = 'Rust';
const res=[];
for (let doc_i in raw_index.documents){
  const doc = raw_index.documents[doc_i];
  const idx = doc.text.indexOf(needle);
  if (-1==idx) { continue; }
  res.push([doc_i | 0,idx]);
}
console.timeEnd("search_text_without_index");

console.log("[D010]", res);

for (let pos of res) {
  const [doc_i, idx] = pos;
  const doc = raw_index.documents[doc_i];

  let title_i = 0;
  for (title_i of doc.title_text_offsets.keys()){
    const offset = doc.title_text_offsets[title_i];
    if (offset >= idx) {break;}
  }

  const title = doc.titles[title_i];
  const link = doc.name + title
  const excerpt_len_around = 30;
  let excerpt_idx_start = idx - excerpt_len_around;
  let excerpt_idx_end = idx + needle.length + excerpt_len_around;
  // while (excerpt_idx_start >= idx - excerpt_len_around && doc.text[excerpt_idx_start] != '\n') excerpt_idx_start--; 
  // while (excerpt_idx_end <= idx + excerpt_len_around && doc.text[excerpt_idx_end] != '\n') excerpt_idx_end++; 

  const excerpt = doc.text.slice(excerpt_idx_start, excerpt_idx_end).trim();
  console.log("[D004]", pos, title_i, doc.name, title, link, "`"+ excerpt + "`");
}
