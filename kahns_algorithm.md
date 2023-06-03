<link rel="stylesheet" type="text/css" href="main.css">

# A lesser known, simple way to find cycles in a graph: Kahn's algorithm

Graphs are everywhere in Software Engineering, or so we are told by Computer Science teachers and interviewers.

There are two things that regularly come up when working with graphs: finding cycles and doing a [topological sort](https://en.wikipedia.org/wiki/Topological_sorting).

> From Wikipedia: A topological sort or topological ordering of a directed graph is a linear ordering of its vertices such that for every directed edge uv from vertex u to vertex v, u comes before v in the ordering. For instance, the vertices of the graph may represent tasks to be performed, and the edges may represent constraints that one task must be performed before another; in this application, a topological ordering is just a valid sequence for the tasks

That's a mouthful but it's not too hard. Here's the graph of employees in an organization. An employee reports to one or more managers, and this forms a graph. The root of the graph is the CEO since they report to no one and so there have no incoming edge:

![Employee hierarchy](kahns_algorithm_1.png)

An arrow (or 'edge') between two nodes means `<source> reports to <destination>`, for example: `Jane the CFO reports to Ellen the CEO`.

A visual inspection shows there are no cycles, and we can even compute the topological sort in our heads:

```
Bella, Software Engineer
Miranda, Software Engineer
Zoe, Software Engineer
Angela, CTO
Jane, CFO
Ellen, CEO
```

The first 3 elements are the ones with no incoming edge, the Software Engineers, since no one reports to them. Then come their repective managers, Angela and Jane. Finally comes their manager, Ellen.

A good command line utility that's already on your (Unix) machine is `tsort`, which takes a list of edges as input, and outputs a topological sort. Here is the input in a text file (`people.txt`):

```
Jane Ellen
Angela Ellen
Zoe Jane
Zoe Angela
Bella Angela
Miranda Angela
```

> `tsort` uses a simple way of defining each edge `A -> B` on its own line with the syntax: `A B`.

and here's the tsort output:

```sh
$ tsort < people.txt
Bella
Miranda
Zoe
Angela
Jane
Ellen
```

And `tsort` also detects cycles, for example if we add the line: `Ellen Zoe` at the end of `people.txt`, we get:

```sh
$ tsort < people.txt
Bella
Miranda
tsort: -: input contains a loop:
tsort: Jane
tsort: Ellen
tsort: Zoe
Jane
tsort: -: input contains a loop:
tsort: Angela
tsort: Ellen
tsort: Zoe
Angela
Ellen
Zoe
```
