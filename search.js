import { raw_index } from './search_index.js';

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

const res = search_text(raw_index, 'bank');
console.log("[D003]", res);

