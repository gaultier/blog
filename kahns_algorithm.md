# A lesser known, simple way to find cycles in a graph: Kahn's algorithm

Graphs are everywhere in Software Engineering, or so we are told by Computer Science teachers and interviewers.

There are two things that regularly come up when working with graphs: finding cycles and doing a [topological sort](https://en.wikipedia.org/wiki/Topological_sorting), that is, get an ordered list of nodes ordered by their number of incoming edges.

That's a mouthful but it's quite simple. Here's the graph of employees in an organization. An employee reports to one or more managers, and this forms a graph. The root of the graph is the CEO since they report to no one and so there have no incoming edge:

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

The first 3 elements are the ones with no incoming edge, the Software Engineers, since no one reports to them.
