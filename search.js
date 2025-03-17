import { raw_index } from './search_index.js';
console.log(index.documents.length);

function search_trigram(search_index, trigram) {
  const v = search_index.index[trigram];
  if (v == undefined) {
    return undefined;
  }

  return new Set(v);
}

function search_text(search_index, text) {
  const results = new Set();

  const runes = Array.from(text);
  if (runes.length < 3){ return undefined; }

  for (let i=2; i<runes.length; i++){
    const trigram = runes[i-2]+runes[i-1]+runes[i];
    const trigram_search = search_trigram(search_index, trigram);
    results = results ? results.intersection(trigram_search) : trigram_search;
  }


  return results;
}

console.log(search_text(search_index, 'most banks'));

