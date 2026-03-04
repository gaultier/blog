let search_index_loading = false;
let search_index = undefined;
const dom_input = document.getElementById('search');
const dom_pseudo_body = document.getElementById('pseudo-body');
const dom_search_matches = document.getElementById('search-matches');
const excerpt_len_around = 100;

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

    const match = search_index.trigram_to_file_idx[trigram];
    if (match === undefined) {
      continue;
    }

    const docs_with_this_trigram = Object.entries(match).length;

    for (const [file, count] of Object.entries(match)) {
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
    search_index_loading = true;
    const response = await fetch('/blog/search_index.json');
    const j = await response.json();
    
    // Build file_idx => file mapping.
    j.idx_to_file = new Map();
    for (const [file, idx] of Object.entries(j.file_to_idx)) {
      j.idx_to_file.set(idx, file);
    }
    search_index = j;
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

  dom_search_matches.innerHTML = '<h3>Search results</h3><ul id="results-list"></ul>';
  const list = document.getElementById('results-list');


  for (const [file_idx, score] of search_results) {
    const file = search_index.idx_to_file.get(Number(file_idx));
    
    // We create a placeholder 'li' immediately so the order stays correct.
    const li = document.createElement('li');
    li.innerHTML = `<a href="${file}">${file}</a> Loading excerpt... (Score: ${score.toFixed(2)})`;
    list.appendChild(li);

    // 3. Fetch the excerpt and update this specific 'li'.
    getExcerpt(file, needle).then(excerpt => {
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
  }
};

dom_input.oninput = search_and_display_results;
