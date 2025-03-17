import search from './search_index.js';
const {raw_index}=search;

function search_unpack_trigram_position(position){
  return {
    document_index : (position >> 0) & 0xfff,
    // offset_start : (position >> 12) & 0xfff,
    // offset_end : (position >> 24) & 0xfff,
    section : (position >> 12) & 0xff,
  }; 
}

function search_trigram(raw_index, trigram) {
  const v = raw_index.index[trigram];
  if (v == undefined) {
    return undefined;
  }

  return new Set(v);
}

function search_text(raw_index, text) {
  let results = undefined;

  const runes = Array.from(text);
  if (runes.length < 3){ return undefined; }

  for (let i=2; i<runes.length; i++){
    const trigram = runes[i-2]+runes[i-1]+runes[i];
    const trigram_search = search_trigram(raw_index, trigram);
    console.log("[D001]", i, trigram, trigram_search, results);
    results = results ? results.intersection(trigram_search) : trigram_search;
    console.log("[D002]", i, trigram, trigram_search, results);
  }


  return results;
}

console.time("search_text_with_index");
const res = search_text(raw_index, 'Odin');
console.timeEnd("search_text_with_index");

console.time("search_text_without_index");
const needle = 'Odin';
const res2=[];
for (let doc of raw_index.documents){
  const idx = doc.text.indexOf(needle);
  if (-1==idx) { continue; }
  res2.push([doc,idx]);
}
console.timeEnd("search_text_without_index");

console.log("[D010]", res2);

console.log("[D003]", res);
for (let pos of res) {
  const unpacked = search_unpack_trigram_position(pos);
  const doc = raw_index.documents[unpacked.document_index];
  const title = doc.titles[unpacked.section];
  const link = doc.name + title
  console.log("[D004]",pos, unpacked, doc.name, title, link);
}

