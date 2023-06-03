# A lesser known, simple way to find cycles in a graph: Kahn's algorithm

Graphs are everywhere in Software Engineering, or so we are told by Computer Science teachers and interviewers.

There are two things that regularly come up when working with graphs: finding cycles and doing a topological sort, that is, get an ordered list of nodes ordered by the number of incoming edges.

That's a mouthful but it's quite simple. Here's the graph of employees in an organization. An employee reports to one or more managers, and this forms a graph. The root of the graph is the CEO since they report to no one and so there have no incoming edge:

![Employee hierarchy](kahns_algorithm_1.png)


