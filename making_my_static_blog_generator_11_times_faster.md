Title: Making my static blog generator 11 times faster
Tags: Optimization, Git, Odin
---

In the early days of this blog, there were only a few articles, and the build process was a simple makefile, something like this (simplified):

```make
%.html: %.md header.html footer.html
        cat header.html >> $@
        pandoc --toc $< >> $@
        cat footer.html >> $@
```

For each markdown file, say `do_not_use_design_patterns.md`, we transform the markdown into HTML (at the time with `pandoc`, now with `cmark`) and save that in a file `do_not_use_design_patterns.html`, with a HTML header and footer.


Later I added the publication date:

```make
%.html: %.md header.html footer.html
        cat header.html >> $@
        printf '<p id="publication_date">Published on %s.</p>\n' $$(git log --format='%as' --reverse -- $< | head -n1)  >> $@; fi
        pandoc --toc $< >> $@
        cat footer.html >> $@
```

The publication date is the creation date, that is: the date of the first Git commit for this file. So we ask Git for the list of commits for this file (they are provided by default from newest to oldest, so we `--reverse` the list), take the first one with `head`, done. It's simple.

*Note: My initial approach was to get the creation and modification date from the file system, but it's incorrect, as soon as you work on more than one machine. The way Git works is that when you pull commits that created a file, it creates the file and does not try to hack the creation date. Thus the file creation date is the time of the Git pull, not the date of the commit that first created it.*

As I added more and more features to this blog, like a list of article by tags, a home page that automatically lists all of the articles, a RSS feed, the 'last modified' date for an article, etc, I outgrew the makefile approach and wrote a small [program](https://github.com/gaultier/blog/blob/master/src/main.odin) (initially in Zig, then in Odin) to do all that. But the core approach remained: 

- List all markdown files in the current directory (i.e. `ls *.md`, make does that for us with `%.md`) 
- For each markdown file, sequentially:
  + Run `git log ... article.md` to get the first and last commit date (respectively 'created at' and 'modified at') 
  + Convert the markdown content to HTML
  + Save this HTML to `article.html`

For long time, it was all good. It was single-threaded, but plenty fast. So I wrote more and more articles. But a few days ago, I noticed that it was getting a bit slow...Just enough for me to wait during the edit-view cycle:

```sh
 $ hyperfine --warmup 2 ./master.bin 
Benchmark 1: ./master.bin
  Time (mean ± σ):      1.873 s ±  0.053 s    [User: 1.351 s, System: 0.486 s]
  Range (min … max):    1.806 s …  1.983 s    10 runs
```

~ 2 seconds is not the end of the world, but it's just enough to be annoying. So what's going on? Let's profile it:

![Profile before the optimization](making_my_static_blog_generator_11_times_faster_profile_before.svg)

Yeah...I think doing `git log` for each article, sequentially, might not be such a good idea. 

So I had to do something about it, because it was going to become slower and slower with each new article. My target was 1s, or possible even 0.5s.

I could see two main options:

- Do not block on `git log`. We can use a thread pool, or a [asynchronous approach](/blog/way_too_many_ways_to_wait_for_a_child_process_with_a_timeout.html) to spawn all the git processes at once, and wait for all of them to finish.
- Make `git log` faster

The last option was my preferred one because it did not force me to re-architect the whole program.

## When there's one, there's many

My intuition was to do a deep dive in the `git log` options, to see if I could instruct it to do less work. But then something struck me: we invoke `git log` to get all commits for one markdown file (even though we only are interested in the first and last, but that's all what Git provides us). What if we invoked it once *for all markdown files*? Yes, the output might be a bit big... Let's try! 

Conceptually we can simply do `git log '*.md'` and parse the output. We can refine that approach later, but that's the gist of it.

Let's see if that's feasible:

```sh
 $ time git log '*.md' | wc -c
191196

________________________________________________________
Executed in   73.69 millis    fish           external
   usr time   61.04 millis  738.00 micros   60.30 millis
   sys time   15.95 millis  191.00 micros   15.76 millis
```

So it's much faster than doing it per file, and also it's entire output is ~ 186 KiB. And these numbers should grow very slowly because each new commit only adds ~ 100 bytes to the output.

Looks like we got our solution.

Mike Acton and Data Oriented Design are right once again:

<img src="mike_acton_dod.png"/>


## The new implementation

We only want for each commit: 

- The date
- Which files were affected

Hence we pass to `git log`:

- `--format='%aI'` to get the date in ISO format
- `--name-status` to know which files this commit added (`A`), modified (`M`), deleted (`D`), or renamed (`R`)
- `--no-merges` to skip merge commits since they do not directly affect any file
- `--diff-filter=AMRD` to only get commits that add/modify/delete/rename files. We are not interested in commits changing the mode of a file, or symlinks, etc.

With these options we get even better numbers:

```sh
$ time git log --format='%aI' --name-status --no-merges --diff-filter=AMDR  -- '*.md' | wc -c
77832

________________________________________________________
Executed in  108.38 millis    fish           external
   usr time   83.70 millis  231.00 micros   83.47 millis
   sys time   27.99 millis  786.00 micros   27.20 millis
```


The output looks like this (I annotated each part along with the commit number):

```
2024-11-05T15:43:44+01:00                                                               | [1] A commit starts with the date.
                                                                                        | [1] Empty line
M       how_to_rewrite_a_cpp_codebase_successfully.md                                   | [1] A list of 1 or more files affected by this commit.
M       write_a_video_game_from_scratch_like_1987.md                                    | [1] It starts with a letter describing the action.
M       x11_x64.md                                                                      | [1] Here it's all modifications.
M       you_inherited_a_legacy_cpp_codebase_now_what.md                                 | [1]
2025-02-02T22:54:23+01:00                                                               | [2] The second entry starts.
                                                                                        | [2] 
R100    cross-platform-timers.md        the_missing_cross_platform_api_for_timers.md    | [2] Rename followed by the confidence score (which we do not care about).
[...]                                                                                   | Etc.
```


Parsing this commit log is tedious but not extremely difficult (why doesn't every mainstream command line tool have a `--json` option in 2025?).

We maintain a map while inspecting each commit: `map<Path, (creation_date, modification_date, tombstone)>`. 

In case of a rename or delete, we set the `tombstone` to `true`. Why not remove the entry from the map directly? Well, we are inspecting the list of commits from newest to oldest.

So first we'll encounter the delete/rename commit, and then a number of add/modify commits for this file. When we are done, we need to remember that this markdown file should be ignored, otherwise, we'll try to open it, read it, and convert it to HTML, but we'll get a `ENOENT` error. We could not maintain a tombstone and just bail on `ENOENT`, that's a matter of taste I guess.

<details>
  <summary>Odin implementation</summary>

```odin
GitStat :: struct {
	creation_date:     string,
	modification_date: string,
	path_rel:          string,
	tombstone:         bool,
}

get_articles_creation_and_modification_date :: proc() -> ([]GitStat, os2.Error) {
	free_all(context.temp_allocator)
	defer free_all(context.temp_allocator)

	state, stdout_bin, stderr_bin, err := os2.process_exec(
		{
			command = []string {
				"git",
				"log",
				// Print the date in ISO format.
				"--format='%aI'",
				// Ignore merge commits since they do not carry useful information.
				"--no-merges",
				// Only interested in creation, modification, renaming, deletion.
				"--diff-filter=AMRD",
				// Show which modification took place:
				// A: added, M: modified, RXXX: renamed (with percentage score), etc.
				"--name-status",
				"*.md",
			},
		},
		context.temp_allocator,
	)
	if err != nil {
		fmt.eprintf("git failed: %d %v %s", state, err, string(stderr_bin))
		panic("git failed")
	}

	stdout := strings.trim_space(string(stdout_bin))
	assert(stdout != "")

	stats_by_path := make(map[string]GitStat, allocator = context.temp_allocator)

	// Sample git output:
	// 2024-10-31T16:09:02+01:00
	// 
	// M       lessons_learned_from_a_successful_rust_rewrite.md
	// A       tip_of_day_3.md
	// 2025-02-18T08:07:55+01:00
	//
	// R100    sha.md  making_my_debug_build_run_100_times_faster.md

	// For each commit.
	for {
		// Date
		date: string
		{
			line := strings.split_lines_iterator(&stdout) or_break

			assert(strings.starts_with(line, "'20"))
			line_without_quotes := line[1:len(line) - 1]
			date = strings.clone(strings.trim(line_without_quotes, "' \n"))
		}

		// Empty line
		{
			// Peek.
			line, ok := strings.split_lines_iterator(&stdout)
			assert(ok)
			assert(line == "")
		}

		// Files.
		for {
			// Start of a new commit?
			if strings.starts_with(stdout, "'20") do break

			line := strings.split_lines_iterator(&stdout) or_break
			assert(line != "")

			action := line[0]
			assert(action == 'A' || action == 'M' || action == 'R' || action == 'D')

			old_path: string
			new_path: string
			{
				// Skip the 'action' part.
				_, ok := strings.split_iterator(&line, "\t")
				assert(ok)

				old_path, ok = strings.split_iterator(&line, "\t")
				assert(ok)
				assert(old_path != "")

				if action == 'R' { 	// Rename has two operands.
					new_path, ok = strings.split_iterator(&line, "\t")
					assert(ok)
					assert(new_path != "")
				} else { 	// The others have only one.
					new_path = old_path
				}
			}

			{
				git_stat, present := &stats_by_path[new_path]
				if !present {
					stats_by_path[new_path] = GitStat {
						// In 99% of the cases, it's *not* a rename and thus the `new_path` should be used.
						path_rel          = new_path,
						// We inspect commits from newest to oldest so the first commit for a file is the newest i.e. the modification date.
						modification_date = date,
						creation_date     = date,
					}
				} else {
					assert(git_stat.path_rel != "")
					assert(git_stat.modification_date != "")
					// Keep updating the creation date, when we reach the end of the commit log, it has the right value.
					git_stat.creation_date = date
				}
			}

			// We handle the action separately from the fact that this is the first commit we see for the path.
			// Because a file could have only one commit which is a rename.
			// Or its first commit is a rename but then there additional commits to modify it. 
			// Case being: these two things are orthogonal.

			if action == 'R' {
				// Mark the old path as 'deleted'.
				stats_by_path[old_path] = GitStat {
					path_rel          = old_path,
					modification_date = date,
					tombstone         = true,
				}

				// The creation date of the new path is the date of the rename operation.
				(&stats_by_path[new_path]).creation_date = date
			}
			if action == 'D' {
				// Mark as 'deleted'.
				(&stats_by_path[new_path]).tombstone = true
			}
		}
	}

	git_stats := make([dynamic]GitStat)
	for _, v in stats_by_path {
		fmt.printf("%v\n", v)
		assert(v.path_rel != "")
		assert(v.creation_date != "")
		assert(v.modification_date != "")
		assert(v.creation_date <= v.modification_date)

		if !v.tombstone {
			append(
				&git_stats,
				GitStat {
					path_rel = strings.clone(v.path_rel),
					creation_date = strings.clone(v.creation_date),
					modification_date = strings.clone(v.modification_date),
				},
			)
		}
	}

	return git_stats[:], nil
}
```

</details

Alright, so how does our new implementation fare compared to the old one?

```sh
 $ hyperfine --warmup 2 './src-main.bin' './src.bin'
Benchmark 1: ./src-main.bin
  Time (mean ± σ):      1.773 s ±  0.022 s    [User: 1.267 s, System: 0.472 s]
  Range (min … max):    1.748 s …  1.816 s    10 runs
 
Benchmark 2: ./src.bin
  Time (mean ± σ):     158.7 ms ±   6.6 ms    [User: 128.4 ms, System: 133.7 ms]
  Range (min … max):   151.7 ms … 175.6 ms    18 runs
 
Summary
  ./src.bin ran
   11.17 ± 0.48 times faster than ./src-main.bin
```

Around 11 times faster, and well within our ideal target of 500 ms !






