Title: Tip of the day #3: Convert a CSV to a markdown or HTML table
Tags: Markdown, CSV, Awk, Tip of the day
---

The other day at work, I found myself having to produce a human-readable table of all the dependencies in the project, for auditing purposes.

There is a [tool](https://github.com/onur/cargo-license) for Rust projects that outputs a TSV (meaning: a CSV where the separator is the tab character) of this data. I just need to convert that to a human readable table in markdown or HTML, and voila!


Here's the output of this tool in my open-source Rust [project](https://github.com/gaultier/kotlin-rs):

```sh
$ cargo license --all-features --avoid-build-deps --avoid-dev-deps --direct-deps-only --tsv
name	version	authors	repository	license	license_file	description
clap	2.33.0	Kevin K. <kbknapp@gmail.com>	https://github.com/clap-rs/clap	MIT		A simple to use, efficient, and full-featured Command Line Argument Parser
heck	0.3.1	Without Boats <woboats@gmail.com>	https://github.com/withoutboats/heck	Apache-2.0 OR MIT		heck is a case conversion library.
kotlin	0.1.0	Philippe Gaultier <philigaultier@gmail.com>				
log	0.4.8	The Rust Project Developers	https://github.com/rust-lang/log	Apache-2.0 OR MIT		A lightweight logging facade for Rust
pretty_env_logger	0.3.1	Sean McArthur <sean@seanmonstar>	https://github.com/seanmonstar/pretty-env-logger	Apache-2.0 OR MIT		a visually pretty env_logger
termcolor	1.1.0	Andrew Gallant <jamslam@gmail.com>	https://github.com/BurntSushi/termcolor	MIT OR Unlicense		A simple cross platform library for writing colored text to a terminal.
```

Not really readable. We need to transform this data into a [markdown table](https://github.github.com/gfm/#tables-extension-), something like that:

```
| First Header  | Second Header |
| ------------- | ------------- |
| Content Cell  | Content Cell  |
| Content Cell  | Content Cell  |
```

Technically, markdown tables are an extension to standard markdown (if there is such a thing), but it's very common. So how do we do that?

Once again, I turn to the trusty AWK. It's always been there for me. And it's present on every UNIX system out of the box.

AWK neatly handles all the 'decoding' of the CSV format for us, we just need to output the right thing:

- Given a line (which AWK calls 'record'): output each field interleaved with the `|` character
- Output a delimiting line between the table headers and rows. The markdown table spec states that this delimiter should be at least 3 `-` characters in each cell.
- Alignement is not a goal, it does not matter for a markdown parser. If you want to produce a pretty markdown table, it's easy to add, it simply makes the implementation a bit bigger

Here's the full implementation (don't forget to mark the file executable). The shebang line instructs AWK to use the tab character `\t` as the delimiter between fields:

```awk
#!/usr/bin/env -S awk -F '\t' -f

{
    printf("|");
    for (i = 1; i <= NF; i++) {
        # Note: if a field contains the character `|`, it will mess up the table. 
        # In this case, we should replace this character by something else e.g. `,`:
        gsub(/\|/, ",", $i);
        printf(" %s |", $i);
    } 
    printf("\n");
} 

NR==1 { # Output the delimiting line
    printf("|");
    for(i = 1; i <= NF; i++) {
        printf(" --- | ");
    }
    printf("\n");
}
```

The first clause will execute for each line of the input. 
The for loop then iterates over each field and outputs the right thing.

The second clause will execute only for the first line (`NR` is the line number). 

The same line can trigger multiple clauses, here, the first line of the input will trigger both clauses, whilst the remaining lines will only trigger the first clause.


So let's run it!

```sh
$ cargo license --all-features --avoid-build-deps --avoid-dev-deps --direct-deps-only --tsv | ./md-table.awk 
| name | version | authors | repository | license | license_file | description |
| --- |  --- |  --- |  --- |  --- |  --- |  --- | 
| clap | 2.33.0 | Kevin K. <kbknapp@gmail.com> | https://github.com/clap-rs/clap | MIT |  | A simple to use, efficient, and full-featured Command Line Argument Parser |
| heck | 0.3.1 | Without Boats <woboats@gmail.com> | https://github.com/withoutboats/heck | Apache-2.0 OR MIT |  | heck is a case conversion library. |
| kotlin | 0.1.0 | Philippe Gaultier <philigaultier@gmail.com> |  |  |  |  |
| log | 0.4.8 | The Rust Project Developers | https://github.com/rust-lang/log | Apache-2.0 OR MIT |  | A lightweight logging facade for Rust |
| pretty_env_logger | 0.3.1 | Sean McArthur <sean@seanmonstar> | https://github.com/seanmonstar/pretty-env-logger | Apache-2.0 OR MIT |  | a visually pretty env_logger |
| termcolor | 1.1.0 | Andrew Gallant <jamslam@gmail.com> | https://github.com/BurntSushi/termcolor | MIT OR Unlicense |  | A simple cross platform library for writing colored text to a terminal. |
```

Ok, it's hard to really know if that's correct or not. Let's pipe it into [cmark-gfm](https://github.com/github/cmark-gfm) to render this markdown table as HTML:

```
$ cargo license --all-features --avoid-build-deps --avoid-dev-deps --direct-deps-only --tsv | ./md-table.awk | cmark-gfm -e table
```

And voila:

<table>
<thead>
<tr>
<th>name</th>
<th>version</th>
<th>authors</th>
<th>repository</th>
<th>license</th>
<th>license_file</th>
<th>description</th>
</tr>
</thead>
<tbody>
<tr>
<td>clap</td>
<td>2.33.0</td>
<td>Kevin K. <a href="mailto:kbknapp@gmail.com">kbknapp@gmail.com</a></td>
<td>https://github.com/clap-rs/clap</td>
<td>MIT</td>
<td></td>
<td>A simple to use, efficient, and full-featured Command Line Argument Parser</td>
</tr>
<tr>
<td>heck</td>
<td>0.3.1</td>
<td>Without Boats <a href="mailto:woboats@gmail.com">woboats@gmail.com</a></td>
<td>https://github.com/withoutboats/heck</td>
<td>Apache-2.0 OR MIT</td>
<td></td>
<td>heck is a case conversion library.</td>
</tr>
<tr>
<td>kotlin</td>
<td>0.1.0</td>
<td>Philippe Gaultier <a href="mailto:philigaultier@gmail.com">philigaultier@gmail.com</a></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>log</td>
<td>0.4.8</td>
<td>The Rust Project Developers</td>
<td>https://github.com/rust-lang/log</td>
<td>Apache-2.0 OR MIT</td>
<td></td>
<td>A lightweight logging facade for Rust</td>
</tr>
<tr>
<td>pretty_env_logger</td>
<td>0.3.1</td>
<td>Sean McArthur <a href="mailto:sean@seanmonstar">sean@seanmonstar</a></td>
<td>https://github.com/seanmonstar/pretty-env-logger</td>
<td>Apache-2.0 OR MIT</td>
<td></td>
<td>a visually pretty env_logger</td>
</tr>
<tr>
<td>termcolor</td>
<td>1.1.0</td>
<td>Andrew Gallant <a href="mailto:jamslam@gmail.com">jamslam@gmail.com</a></td>
<td>https://github.com/BurntSushi/termcolor</td>
<td>MIT OR Unlicense</td>
<td></td>
<td>A simple cross platform library for writing colored text to a terminal.</td>
</tr>
</tbody>
</table>


All in all, very little code. I have a feeling that I will use this approach a lot in the future for reporting or even inspecting data easily, for example from a database dump.
