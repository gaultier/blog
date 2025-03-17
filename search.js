import { raw_index } from './search_index.js';
console.log(raw_index.documents.length);

function search_trigram(raw_index, trigram) {
  const v = raw_index.index[trigram];
  if (v == undefined) {
    return undefined;
  }

  return new Set(v);
}

function search_text(raw_index, text) {
  const results = new Set();

  const runes = Array.from(text);
  if (runes.length < 3){ return undefined; }

  for (let i=2; i<runes.length; i++){
    const trigram = runes[i-2]+runes[i-1]+runes[i];
    const trigram_search = search_trigram(raw_index, trigram);
    results = results ? results.intersection(trigram_search) : trigram_search;
  }


  return results;
}

const res = search_text(raw_index, 'most banks');
console.log(res);

