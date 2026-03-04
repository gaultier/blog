let search_index_loading = false;
let search_index = undefined;
const dom_input = document.getElementById('search');
const dom_pseudo_body = document.getElementById('pseudo-body');
const dom_search_matches = document.getElementById('search-matches');
const excerpt_len_around = 100;

class PostcardDecoder {
  /**
   * @param {Uint8Array} bytes The encoded postcard data
   */
  constructor(bytes) {
    this.view = bytes;
    this.pos = 0;
  }

  /**
   * Decodes a Varint (used for all unsigned integers and lengths)
   * Postcard uses a LEB128-like encoding.
   */
  uInt() {
    let value = 0n;
    let shift = 0n;
    while (true) {
      const byte = this.view[this.pos++];
      value |= BigInt(byte & 0x7f) << shift;
      if ((byte & 0x80) === 0) break;
      shift += 7n;
    }
    // Return as Number if it fits, otherwise BigInt
    return value <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(value) : value;
  }

  // Individual unsigned types all use the same varint encoding in Postcard
  u8()  { return this.view[this.pos++]; } // u8 is the only fixed-size uint
  u16() { return this.uInt(); }
  u32() { return this.uInt(); }
  u64() { return this.uInt(); }

  /**
   * Decodes a string: a varint length followed by UTF-8 bytes.
   */
  string() {
    const len = this.uInt();
    const bytes = this.view.slice(this.pos, this.pos + len);
    this.pos += len;
    return new TextDecoder().decode(bytes);
  }

  /**
   * Decodes a sequence (seq): a varint length followed by N elements.
   * @param {Function} decoderFn The decoder method for the element type.
   */
  seq(decoderFn) {
    const len = this.uInt();
    const arr = new Array(len);
    for (let i = 0; i < len; i++) {
      arr[i] = decoderFn.call(this);
    }
    return arr;
  }

  /**
   * Decodes a tuple: N elements without a length prefix.
   * @param {Array<Function>} decoderFns Ordered list of decoders for tuple elements.
   */
  tuple(decoderFns) {
    return decoderFns.map(fn => fn.call(this));
  }

  /**
   * Decodes a struct: basically a tuple with named keys.
   * @param {Object} schema Key-value pairs of field names and decoder functions.
   */
  struct(schema) {
    const obj = {};
    for (const [key, decoderFn] of Object.entries(schema)) {
      obj[key] = decoderFn.call(this);
    }
    return obj;
  }

  /**
   * Decodes a map: a varint length followed by N key-value pairs.
   */
  map(keyDecoder, valueDecoder) {
    const len = this.uInt();
    const map = new Map();
    for (let i = 0; i < len; i++) {
      const k = keyDecoder.call(this);
      const v = valueDecoder.call(this);
      map.set(k, v);
    }
    return map;
  }
}

async function getExcerpt(url, needle) {
  const response = await fetch(url);
  const html = await response.text();

  // Create a temporary DOM element to strip HTML tags
  const doc = new DOMParser().parseFromString(html, 'text/html');
  const root = doc.getElementById('pseudo-body');
  const ignore = ['.article-prelude', '.article-title', '.toc', 'script', 'style'];
  ignore.forEach(selector => {
    root.querySelectorAll(selector).forEach(el => el.remove());
  });
  const text = root.textContent.replace(/\s+/g, ' ').trim();
  const lowerText = text.toLowerCase();
  const lowerNeedle = needle.toLowerCase();
  const index = lowerText.indexOf(lowerNeedle);
  if (index === -1) {
     return '';
   }

  const start = Math.max(0, index - 60);
  const end = Math.min(text.length, index + 100);
    
  // Simple bolding of the match
  let snippet = text.slice(start, end);
  const regex = new RegExp(`(${needle})`, 'gi');
  snippet = snippet.replace(regex, '<strong>$1</strong>');

  return `...${snippet}...`; 
}

function search_text(needle) {
  needle = needle.toLowerCase();
  console.time("search_text");

  const file_scores = new Map();

  for (let i = 0; i <= needle.length - 3; i++) {
    const trigram = needle[i] + needle[i+1] + needle[i+2];

    const match = search_index.trigram_to_file_idx.get(trigram);
    if (match === undefined) {
      continue;
    }

    const docs_with_this_trigram = match.length;

    for (const m of match) {
      const [file, count] = m;
      const score = file_scores.get(file) || 0;
      file_scores.set(file, score + count * (1/docs_with_this_trigram) );
    }
  }
  
  console.timeEnd("search_text");
  return file_scores;
}

async function loadSearchIndex() {
  if (search_index !== undefined || search_index_loading) {return;}

  dom_input.placeholder = 'Loading search index...';

  try {
    console.time('load search index');
    search_index_loading = true;
    const response = await fetch('/blog/search_index.postcard');
    const buffer = await response.arrayBuffer();
    const decoder = new PostcardDecoder(new Uint8Array(buffer));
    const trigramToFileIdx = decoder.map(
      // Key: Trigram (String).
      decoder.string, 
      // Value: Vec<(FileIdx, u32)>.
      () => decoder.seq(() => {
        // Each element in the Vec is a tuple (u16, u32).
        return decoder.tuple([decoder.u16, decoder.u32]);
      })
    );

    // 2. Decode 'files' (Vec<String>).
    const files = decoder.seq(decoder.string);

    search_index = {
      trigram_to_file_idx: trigramToFileIdx,
      idx_to_file: new Map(),
    }; 
    // Build mapping of: file_idx => file.
    for (const [idx, file] of files.entries()) {
      search_index.idx_to_file.set(idx, file);
    }
    console.timeEnd('load search index');
    search_index_loading = false;

    dom_input.placeholder = 'Search index loaded!';
  } catch(e) {
    dom_input.placeholder = 'Search index failed to load!';
    console.error(e);
  }
}

dom_input.addEventListener('focus', loadSearchIndex);
dom_input.addEventListener('click', loadSearchIndex);

async function search_and_display_results(_event) {
  const needle = dom_input.value;
  dom_search_matches.innerHTML = '';
  
  if (needle.length < 3) {
    dom_pseudo_body.hidden = false;
    dom_search_matches.hidden = true;
    return;
  } else {
    dom_pseudo_body.hidden = true;
    dom_search_matches.hidden = false;
  }

  const scores = search_text(needle);
  // Sort by score DESC.
  const search_results = [...scores.entries()].filter(([_, score]) => score !== 0).sort((a, b) => b[1] - a[1]);
  if (search_results.length === 0) {
    dom_search_matches.innerHTML = '<h3>Search results</h3><p>No results found.</p>';
    return;
  }

  dom_search_matches.innerHTML = '<h3>Search results</h3><ul id="results-list"></ul>';
  const list = document.getElementById('results-list');

  const excerptPromises = [];

  for (const [file_idx, score] of search_results) {
    const file = search_index.idx_to_file.get(Number(file_idx));
    
    // We create a placeholder 'li' immediately so the order stays correct.
    const li = document.createElement('li');
    li.innerHTML = `<a href="${file}">${file}</a> Loading excerpt... (Score: ${score.toFixed(2)})`;
    list.appendChild(li);

    // 3. Fetch the excerpt and update this specific 'li'.
    const p = getExcerpt(file, needle).then(excerpt => {
        if (excerpt === ''){
          li.hidden = true;
        } else {
          li.innerHTML = `
              <a href="${file}">${file}</a> 
              (Score: ${score.toFixed(2)})
              <p><small>${excerpt}</small></p>
          `;
        }
    }).catch(() => {
        li.innerHTML = `<a href="${file}">${file}</a> (Score: ${score.toFixed(2)})`;
    });

    excerptPromises.push(p);
  }

  await Promise.allSettled(excerptPromises);
  const visibleItems = Array.from(list.children).filter(li => !li.hidden);
  if (visibleItems.length === 0) {
    list.insertAdjacentHTML('afterend', '<p id="no-results-msg">No results found.</p>');
  }
}

dom_input.oninput = search_and_display_results;
