Title: Making my static blog generator 11 times faster
Tags: Optimization, Git, Odin, Fossil
---


This blog is statically generated from Markdown files. It used to be fast, but nowadays it's not:

```sh
 $ hyperfine --warmup 2 ./master.bin 
Benchmark 1: ./master.bin
  Time (mean ± σ):      1.873 s ±  0.053 s    [User: 1.351 s, System: 0.486 s]
  Range (min … max):    1.806 s …  1.983 s    10 runs
```

~ 2 seconds is not the end of the world, but it's just enough to be annoying when doing lots of edit-view cycles. Worse, it seemed to become slower and slower as I wrote more articles. So today I finally dedicated some time to tackle this problem.

## The investigation

In the early days of this blog, there were only a few articles, and the build process was a simple Makefile, something like this (simplified):

```make
%.html: %.md header.html footer.html
        cat header.html >> $@
        pandoc --toc $< >> $@
        cat footer.html >> $@
```

For each markdown file, say `wayland_from_scratch.md`, we transform the markdown into HTML (at the time with `pandoc`, which proved to be super slow, now with `cmark` which is extremely fast) and save that in the file `wayland_from_scratch.html`, with a HTML header prepended and footer appended.


Later on, I added the publication date:

```make
%.html: %.md header.html footer.html
        cat header.html >> $@
        printf '<p id="publication_date">Published on %s.</p>\n' $$(git log --format='%as' --reverse -- $< | head -n1)  >> $@; fi
        pandoc --toc $< >> $@
        cat footer.html >> $@
```

The publication date is the creation date, that is: the date of the first Git commit for this file. So we ask Git for the list of commits for this file (they are provided by default from newest to oldest, so we `--reverse` the list), take the first one with `head`, done. It's simple.

*Note: My initial approach was to get the creation and modification date from the file system, but it's incorrect, as soon as you work on more than one machine. The way Git works is that when you pull commits that created a file, it creates the file on the file system and does not try to hack the creation date. Thus the file creation date is the time of the Git pull, not the date of the commit that first created it.*

As I added more and more features to this blog, like a list of article by tags, a home page that automatically lists all of the articles, a RSS feed, the 'last modified' date for an article, etc, I outgrew the Makefile approach and wrote a small [program](https://github.com/gaultier/blog/blob/master/src/main.odin) (initially in Zig, then in [Odin](https://odin-lang.org/)) to do all that. But the core approach remained: 

- List all markdown files in the current directory (e.g. `ls *.md`, the Makefile did that for us with `%.md`) 
- For each markdown file, sequentially:
  + Run `git log article.md` to get the date of the first and last commits for this file (respectively 'created at' and 'modified at') 
  + Convert the markdown content to HTML
  + Save this HTML to `article.html`

For long time, it was all good. It was single-threaded, but plenty fast. So I wrote more and more articles. But now it's too slow. Why? Let's profile it:

<object alt="Profile before the optimization" data="making_my_static_blog_generator_11_times_faster_profile_before.svg" type="image/svg+xml" style="width:50%; display: block; margin: auto" />

Yeah...I think it might be [git] [git] [git] [git] [git] [git] [git] [git]...


Another way to confirm this is with `strace`:

```sh
$ strace --summary-only ./src.bin
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 94.85    0.479290        3928       122           waitid
[...]
```

So ~ **95 %** of the running time is spent waiting on a subprocess. It's mainly git - we also run `cmark` as a subprocess but it's really really fast. We could further investigate with `strace` which process we are waiting on but the CPU profile already points the finger at Git and `cmark` is not even visible on it.

At this point it's important to mention that this program is a very simplistic static site generator: it is stateless and processes every markdown file in the repository one by one. You could say that it's a regression compared to the Makefile because `make` has built-in parallelism with `-j` and change detection. But in reality, change detection in make is primitive and I often want to reprocess everything because of a change that applies to every file. For example, I reword the `Donate` section at the end of each article (wink wink), or the header, or the footer, etc.

Also, I really am fond of this 'pure function' approach. There is no caching issue to debug, no complicated code to write, no data races, no async callbacks, etc.

My performance target was to process every file within 1s, or possibly even 0.5s.

I could see a few options:

- Do not block on `git log`. We can use a thread pool, or an [asynchronous approach](/blog/way_too_many_ways_to_wait_for_a_child_process_with_a_timeout.html) to spawn all the git processes at once, and wait for all of them to finish. But it's more complex.
- Implement caching so that only the changed markdown files get regenerated.
- Make `git log` faster somehow.

The last option was my preferred one because it did not force me to re-architect the whole program.

Note that the other two options are sill on the table regardless of whether our experiment works out or not.

## When there's one, there's many

My intuition was to do a deep dive in the `git log` options, to see if I could instruct it to do less work. But then something struck me: we invoke `git log` to get all commits for one markdown file (even though we only are interested in the first and last, but that's the only interface Git provides us as far as I know). What if we invoked it once *for all markdown files*? Yes, the output might be a bit big... How big? Is it really faster? Let's measure! 

Conceptually we can simply do `git log '*.md'` and parse the output. We can refine that approach later with more options, but that's the gist of it:

```sh
 $ time git log '*.md' | wc -c
191196

________________________________________________________
Executed in   73.69 millis    fish           external
   usr time   61.04 millis  738.00 micros   60.30 millis
   sys time   15.95 millis  191.00 micros   15.76 millis
```

So it's much faster than doing it per file, and also it's entire output is ~ 186 KiB. And these numbers should grow very slowly because each new commit only adds 20-100 bytes to the output.

Looks like we got our solution. There is one added benefit: we do not need to list all `.md` files in the directory at startup. Git gives us this information (in my case there are no markdown files *not* tracked by Git).

Mike Acton and [Data Oriented Design](https://en.wikipedia.org/wiki/Data-oriented_design) are right once again:

> Rule of thumb: When there is one, there is many. [^1]

Or: try to think in terms of arrays, not in terms of one isolated object at a time.


## The new approach

We only want git to tell us, for each commit: 

- The date
- Which files were affected

Hence we pass to `git log`:

- `--format='%aI'` to get the date in ISO format
- `--name-status` to know which files this commit added (`A`), modified (`M`), deleted (`D`), or renamed (`R`)
- `--no-merges` to skip merge commits since they do not directly affect any file
- `--diff-filter=AMRD` to only get commits that add/modify/delete/rename files. We are not interested in commits changing the permissions on a file, or modifying symbolic links, etc.

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
2024-11-05T15:43:44+01:00                                                            | [1] A commit starts with the date.
                                                                                     | [1] Empty line
M       how_to_rewrite_a_cpp_codebase_successfully.md                                | [1] A list of files affected by this commit.
M       write_a_video_game_from_scratch_like_1987.md                                 | [1] Each starts with a letter describing the action.
M       x11_x64.md                                                                   | [1] Here it's all modifications.
M       you_inherited_a_legacy_cpp_codebase_now_what.md                              | [1]
2025-02-02T22:54:23+01:00                                                            | [2] The second entry starts.
                                                                                     | [2] 
R100    cross-platform-timers.md        the_missing_cross_platform_api_for_timers.md | [2] Rename with the (unneeded) confidence score.
[...]                                                                                | Etc.
```


Parsing this commit log is tedious but not extremely difficult.

We maintain a map while inspecting each commit: `map<Path, (creation_date, modification_date, tombstone)>`. 

In case of a rename or delete, we set the `tombstone` to `true`. Why not remove the entry from the map directly? Well, we are inspecting the list of commits from newest to oldest.
So first we'll encounter the delete/rename commit for this file, and then later in the stream, a number of add/modify commits. When we are done, we need to remember that this markdown file should be ignored, otherwise, we'll try to open it, read it, and convert it to HTML, but we'll get a `ENOENT` error because it does not exist anymore on disk. We could avoid having this tombstone field and just bail on `ENOENT`, that's a matter of taste I guess, but this field was useful to me to ensure that the parsing code is correct.

Alternatively, we could pass `--reverse` to `git log` and parse the commits in chronological order. When we see a delete/rename commit for a file, we can safely remove the entry from the map since no more commits about this file should show up after that.

## The new implementation

```odin
GitStat :: struct {
	creation_date:     string,
	modification_date: string,
	path_rel:          string,
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

	GitStatInternal :: struct {
		creation_date:     string,
		modification_date: string,
		tombstone:         bool,
	}
	stats_by_path := make(map[string]GitStatInternal, allocator = context.temp_allocator)

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
					stats_by_path[new_path] = GitStatInternal {
						// We inspect commits from newest to oldest so the first commit for a file is the newest i.e. the modification date.
						modification_date = date,
						creation_date     = date,
					}
				} else {
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
				stats_by_path[old_path] = GitStatInternal {
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
	for k, v in stats_by_path {
		assert(k != "")
		assert(v.creation_date != "")
		assert(v.modification_date != "")
		assert(v.creation_date <= v.modification_date)

		if !v.tombstone {
			git_stat := GitStat {
				path_rel          = strings.clone(k),
				creation_date     = strings.clone(v.creation_date),
				modification_date = strings.clone(v.modification_date),
			}
			fmt.printf("%v\n", git_stat)
			append(&git_stats, git_stat)
		}
	}

	return git_stats[:], nil
}
```

A few things of interest:

- Odin has first class support for allocators so we allocate everything in this function with the temporary allocator. It is backed by an arena and emptied at the start and end of the function. Only the final result is allocated with the standard allocator. That way, even if Git starts spewing lots of data, as soon as we exit the function, all of that is gone, in one call, and the the program carries on with only the necessary data heap-allocated.
- In this program, the main allocator and the temporary allocator are both arenas. The memory usage is a constant ~ 4 MiB, mainly located in the Odin standard library. The memory usage of my code is around ~ 65 KiB.
- A `map` is a bit of an overkill for ~30 entries, but it's fine, and we expect the number of articles to grow

---

We can log the final result:

```
[...]
GitStat{creation_date = "2020-09-07T20:49:20+02:00", modification_date = "2024-11-04T09:24:17+01:00", path_rel = "compile_ziglang_from_source_on_alpine_2020_9.md"}
GitStat{creation_date = "2024-09-10T12:59:04+02:00", modification_date = "2024-09-12T12:14:42+02:00", path_rel = "odin_and_musl.md"}
GitStat{creation_date = "2023-11-23T11:26:11+01:00", modification_date = "2025-02-06T20:55:27+01:00", path_rel = "roll_your_own_memory_profiling.md"}
[...]
```

Alright, so how does our new implementation fare compared to the old one?


First, we can confirm with `strace` that the time spent on waiting for subprocesses (mainly Git) shrinked:

```sh
$ strace --summary-only ./src.bin
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 56.59    0.043176         674        64           waitid
 [...]
```

Then we benchmark:

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


Around 11 times faster, and well within our ideal target of 500 ms ! And all we had to do was convert many `git log` invocations (one per markdown file) to just one. Pretty simple change, located in one function. Almost all of the complexity is due to parsing Git custom text output and skipping over irrelevant commits. We don't really have a choice either: that's all Git provides to query the commit log. The alternatives are all worse:

- Parse directly the Git object files - no thank you
- Use a library (e.g. `libgit2`) and hope that it offers a saner interface to query the commit log 

I wonder if there is a better way...

## Fossil

[fossil](https://fossil-scm.org) is an alternative version control system created by the same folks that created, and are still working on, SQLite. Naturally, a fossil repository is basically just one SQLite database file. That sounds very *queryable*!

Let's import our git repository into a Fossil repository and enter the SQLite prompt:

```sh
$ git fast-export --all | fossil import --git new-repo.fossil
$ file new-repo.fossil 
new-repo.fossil: SQLite 3.x database (Fossil repository), [...]
$ fossil sql -R new-repo.fossil
```

There are lots of tables in this database. We craft this query after a few trials and errors (don't know if it is optimal or not):

```sql
sqlite> .mode json
sqlite> SELECT 
            f.name as filename,
            datetime(min(e.mtime)) as creation_date,
            datetime(max(e.mtime)) as last_modified
        FROM repository.filename f
        JOIN repository.mlink m ON f.fnid = m.fnid
        JOIN repository.event e ON m.mid = e.objid
        WHERE filename LIKE '%.md'
        GROUP BY f.name
        ORDER BY f.name;
```

Which outputs what we want:

```
[...]
{"filename":"body_of_work.md","creation_date":"2023-12-19 13:27:40","last_modified":"2024-11-05 15:11:55"},
{"filename":"communicate_by_sharing_code.md","creation_date":"2024-03-07 09:48:39","last_modified":"2024-03-07 10:14:09"},
{"filename":"compile_ziglang_from_source_on_alpine_2020_9.md","creation_date":"2020-09-07 18:49:20","last_modified":"2024-11-04 08:24:17"},
[...]
```

Note that this does not filter out deleted/removed files yet. I'm sure that it can be done by tweaking the query a bit, but there's not time! We need to benchmark!

```sh
$ hyperfine --shell=none 'fossil sql -R new-repo.fossil "SELECT [...]"'
Benchmark 1: fossil sql -R new-repo.fossil "[...]"
  Time (mean ± σ):       3.0 ms ±   0.5 ms    [User: 1.5 ms, System: 1.4 ms]
  Range (min … max):     2.2 ms …   5.6 ms    776 runs
```

Damn that's fast.

I do not use Fossil, but I eye it from time to time - generally when I need to extract some piece of information from Git and I discover it does not let me, or when I see the Gitlab bill  my (ex-) company pays, or when the Jira page takes more than 10 seconds to load... yeah, Fossil is the complete package, with issues, forums, a web UI, a timeline, a wiki, a chat... it has it all!

But the golden ticket idea is really to store everything inside SQLite. Suddenly, we can query anything! And there is no weird parsing needed - SQLite supports various export formats and (some? all?) fossil commands support the `--sql` option to show you which SQL query they use to get the information. After all, the only thing the `fossil` command line does in this case, is craft a SQL query and run int on the SQLite database.

It's quite magical to me that I can within a few seconds import my 6 years-long git repository into a SQLite database and start querying it, and the performance is great.

Now I am not *quite* ready yet to move to Fossil, and the import is a one time thing as far as I know, so it is not a viable option for the problem at hand as long as git is the source of truth. But still, while I was trying to tackle `git log` into submission, I was thinking the whole time: why can't I do an arbitrary query of git data? Generally, the more generic approach is slower than the ad-hoc one, but here it's not even the case. Fossil is for this use case objectively more powerful, more generic, *and* faster.

## Conclusion

The issue was effectively a N+1 query problem. We issued a separate 'query' (in this case, `git log`) for each markdown file, in a sequential blocking fashion. This approach worked until it didn't because the number of entities (i.e. articles, and commits) grew over time. 

The solution is instead to do only one query for all entities. It may return a bit more data that we need, but that's much faster, and scales better, than the original version.

It's obvious in retrospect but it's easy to let it happen when the 'codebase' (a big word for only one file that started as a basic Makefile) is a few years old, it's only looked at briefly from time to time, and the initial implementation did not really allow for the correct approach - who wants to parse the `git log` output in the Makefile language? 

Furthermore, the initial approach was fine because it only looked at the creation date, so we could do `git log --reverse article.md | head -n1` which is faster than sifting through the whole commit log for this file. However, as it is always the case, requirements (again, a big word for: my taste concerning what should my personal blog look like) change and the modification date now had to also be extracted from git. This forced us, with the current Git functionality, to scan through the whole commit log, for each file, which became too slow.

As Mike Acton and Data Oriented Design state: 

> Different problems require different solutions.[^2]

And:


> If you have different data, you have a different problem. [^3]

It also does not help that any querying in Git is ad-hoc and outputs a weird text format that we have to tediously parse. Please everyone, let's add the option to output structured data in our command line programs, damn it! String programming is no fun at all - that's why I moved away from concatenating the output of shell commands in a Makefile, to a real programming language, to do the static generation.


All in all, I am pleased with my solution - I can now see any edit materialize *instantly*. It's a bit funny that my previous article was about SIMD and inspecting assembly instructions, while this issue is so obvious and high-level in retrospect.


To the next 5 years of blogging, till I need to revisit the performance of this function!



[^1]: [CppCon 2014: Mike Acton "Data-Oriented Design and C++"](https://www.youtube.com/watch?v=rX0ItVEVjHc&t=14m33s)
[^2]: [CppCon 2014: Mike Acton "Data-Oriented Design and C++"](https://www.youtube.com/watch?v=rX0ItVEVjHc&t=13m13s)
[^3]: [CppCon 2014: Mike Acton "Data-Oriented Design and C++"](https://www.youtube.com/watch?v=rX0ItVEVjHc&t=13m21s)



