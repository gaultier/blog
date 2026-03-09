let search_content_loaded = false;
const dom_input = document.getElementById("search");
const dom_pseudo_body = document.getElementById("pseudo-body");
const dom_search_matches = document.getElementById("search-matches");
const excerpt_len_around = 100;

const html_files = {
  "a_million_ways_to_data_race_in_go.html": ["", ""],
  "a_small_trick_to_improve_technical_discussions_by_sharing_code.html": [
    "",
    "",
  ],
  "a_subtle_data_race_in_go.html": ["", ""],
  "addressing_cgo_pains_one_at_a_time.html": ["", ""],
  "advent_of_code_2018_5_revisited.html": ["", ""],
  "advent_of_code_2018_5.html": ["", ""],
  "an_amusing_go_static_analysis_blindspot.html": ["", ""],
  "an_optimization_and_debugging_story_go_dtrace.html": ["", ""],
  "are_my_sql_files_read_at_build_time_or_run_time.html": ["", ""],
  "articles-by-tag.html": ["", ""],
  "body_of_work.html": ["", ""],
  "build-pie-executables-with-pie.html": ["", ""],
  "compile_ziglang_from_source_on_alpine_2020_9.html": ["", ""],
  "detecting_goroutine_leaks_with_dtrace.html": ["", ""],
  "feed.html": ["", ""],
  "gnuplot_lang.html": ["", ""],
  "go_dtrace_see_all_network_traffic.html": ["", ""],
  "go_dtrace_see_registered_routes.html": ["", ""],
  "how_to_reproduce_and_fix_an_io_data_race_with_dtrace.html": ["", ""],
  "how_to_rewrite_a_cpp_codebase_successfully.html": ["", ""],
  "image_size_reduction.html": ["", ""],
  "index.html": ["", ""],
  "kahns_algorithm.html": ["", ""],
  "lessons_learned_from_a_successful_rust_rewrite.html": ["", ""],
  "making_my_debug_build_run_100_times_faster.html": ["", ""],
  "making_my_static_blog_generator_11_times_faster.html": ["", ""],
  "observe_sql_queries_in_go_with_dtrace.html": ["", ""],
  "odin_and_musl.html": ["", ""],
  "perhaps_rust_needs_defer.html": ["", ""],
  "roll_your_own_memory_profiling.html": ["", ""],
  "rust_c++_interop_trick.html": ["", ""],
  "rust_underscore_vars.html": ["", ""],
  "shell_pitfall.html": ["", ""],
  "speed_up_your_ci.html": ["", ""],
  "subtle_bug_with_go_errgroup.html": ["", ""],
  "the_missing_cross_platform_os_api_for_timers.html": ["", ""],
  "the_production_bug_that_made_me_care_about_undefined_behavior.html": [
    "",
    "",
  ],
  "tip_of_day_1.html": ["", ""],
  "tip_of_day_3.html": ["", ""],
  "tip_of_the_day_2.html": ["", ""],
  "tip_of_the_day_4.html": ["", ""],
  "tip_of_the_day_5.html": ["", ""],
  "tip_of_the_day_6.html": ["", ""],
  "way_too_many_ways_to_wait_for_a_child_process_with_a_timeout.html": ["", ""],
  "wayland_from_scratch.html": ["", ""],
  "what_should_your_mutexes_be_named.html": ["", ""],
  "write_a_video_game_from_scratch_like_1987.html": ["", ""],
  "x11_x64.html": ["", ""],
  "you_inherited_a_legacy_cpp_codebase_now_what.html": ["", ""],
};

function getExcerpt(file, needle) {
  const sizeAround = 150;
  const [text, lowerText] = html_files[file];
  const index = lowerText.indexOf(needle);
  if (index === -1) {
    return "";
  }

  const start = Math.max(0, index - sizeAround);
  const end = Math.min(text.length, index + sizeAround);

  // Simple bolding of all matches.
  const snippet = text.slice(start, end);
  const escapedNeedle = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const regex = new RegExp(`(${escapedNeedle})`, "gi");
  return "..." + snippet.replace(regex, '<strong class="search-match">$1</strong>') + "...";
}

function load_search_content() {
  if (search_content_loaded) {return;}

  const search_input = document.getElementById("search");

  const allPromises = new Array();

  for (const [file, _] of Object.entries(html_files)) {
    search_input.placeholder = "Loading search index...";

    // 3. Fetch the excerpt and update this specific 'li'.
    const promise = fetch(file)
      .then((r) => r.text())
      .then((html) => {
        const doc = new DOMParser().parseFromString(html, "text/html");
        const root = doc.getElementById("pseudo-body");
        const ignore = [
          ".article-prelude",
          ".article-title",
          ".toc",
          "script",
          "style",
          "code",
        ];
        ignore.forEach((selector) => {
          root.querySelectorAll(selector).forEach((el) => el.remove());
        });
        const text = root.textContent.trim();
        html_files[file][0] = text;
        html_files[file][1] = text.toLowerCase();
      }).catch((e) => {
        console.error(e);
      });
    allPromises.push(promise);
  }

  Promise.allSettled(allPromises).then(_ => { search_input.placeholder = "Search index loaded!"; search_content_loaded = true; });
}

dom_input.addEventListener("focus", load_search_content);
dom_input.addEventListener("click", load_search_content);

async function search_and_display_results(_event) {
  const start = performance.now();

  const needle = dom_input.value.toLowerCase();
  dom_search_matches.innerHTML = "";

  if (needle.length < 3) {
    dom_pseudo_body.hidden = false;
    dom_search_matches.hidden = true;
    return;
  } else {
    dom_pseudo_body.hidden = true;
    dom_search_matches.hidden = false;
  }

  dom_search_matches.innerHTML =
    '<h3>Search results</h3><ul id="results-list"></ul>';
  const list = document.getElementById("results-list");

  let count = 0;
  for (const [file, _] of Object.entries(html_files)) {
    const excerpt = getExcerpt(file, needle);
    if (excerpt !== "") {
      const li = document.createElement("li");
      li.innerHTML = `<a href="${file}">${file}</a>`;
      list.appendChild(li);
      li.innerHTML = `
              <a href="${file}">${file}</a> 
              <p><small>${excerpt}</small></p>
          `;
      count += 1;
    }
  }

  if (count === 0) {
    list.insertAdjacentHTML(
      "afterend",
      '<p id="no-results-msg">No results found (code snippets are not searched).</p>',
    );
  }


  const end = performance.now();
  const duration = end - start;
  console.log(`search: ${duration.toFixed(4)} ms`);
}

dom_input.oninput = search_and_display_results;
