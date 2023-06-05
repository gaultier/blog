<link rel="stylesheet" type="text/css" href="main.css">
<link rel="stylesheet" href="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/styles/default.min.css">
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/highlight.min.js"></script>
<script>
window.addEventListener("load", (event) => {
  hljs.highlightAll();
});
</script>

<a href="/blog">All articles</a>

# Cycle detection in graphs does not have to be hard: A lesser known, simple way with Kahn's algorithm

**Table of Contents**
- [Introduction](#introduction)
- [The database](#the-database)
- [Topological sort](#topological-sort)
- [How to store the graph in memory](#how-to-store-the-graph-in-memory)
- [Kahn's algorithm](#kahns-algorithm)
- [Implementation](#implementation)
  - [Helpers](#helpers)
  - [The algorithm](#the-algorithm)
  - [Inserting entries in the database](#inserting-entries-in-the-database)
  - [Detecting cycles](#detecting-cycles)
  - [Detecting multiple roots](#detecting-multiple-roots)
- [Playing with the database](#playing-with-the-database)
- [Closing thoughts](#closing-thoughts)
- [Addendum: the full code](#addendum-the-full-code)

## Introduction 

> If you spot an error, please open a [Github issue](https://github.com/gaultier/blog)!

Graphs are everywhere in Software Engineering, or so we are told by Computer Science teachers and interviewers. But sometimes, they do show up in real problems.

Not too long ago, I was tasked to create a Web API to create and update a company's hierarchy of employee, and display that on a web page. Basically, who reports to whom.

In the simple case, it's a tree, when an employee reports to exactly one manager.

![Employee hierarchy](kahns_algorithm_1.svg)

Here's the tree of employees in an organization. An employee reports to a manager, and this forms a tree. The root is the CEO since they report to no one and so they have no outgoing edge.

An arrow (or 'edge') between two nodes means `<source> reports to <destination>`, for example: `Jane the CFO reports to Ellen the CEO`.

 But here is the twist: our API receives a list of `employee -> manager` links, in any order:

 ```
Jane -> Ellen
Angela -> Ellen
Zoe -> Jane
Zoe -> Angela
Bella -> Angela
Miranda -> Angela
 ```

It opens the door to various invalid inputs: links that form a graph (an employee has multiple managers), multiple roots (e.g. multiple CEOs) or cycles.

 We have to detect those and reject them, such as this one:

 
![Invalid employee hierarchy](kahns_algorithm_1_invalid.svg)


## The database

So how do we store all of those people in the database?

```sql
CREATE TABLE IF NOT EXISTS people(name TEXT NOT NULL UNIQUE, manager BIGINT REFERENCES people)
```

Each employee has a optional reference to a manager. 


> This is not a novel idea, actually this is one of the examples in the official [SQLite documentation](https://www.sqlite.org/lang_with.html).

For example, to save `Ellen, CEO` inside the database, we do:

```sql
INSERT INTO people VALUES('Ellen, CEO', NULL)
```

And to save `Jane, CFO` in the database:

```sql
INSERT INTO people VALUES('Jane, CFO', 1)
```

Where `Ellen, CEO`, Jane's boss, which we just inserted before, has the id `1`.

Immediately, we notice that to insert an employee, their manager needs to already by in the database, by virtue of the self-referential foreign key `manager BIGINT REFERENCES people`.

So we need a way to sort the big list of `employee -> manager` links (or 'edges' in graph parlance), to insert them in the right order. First we insert the CEO, who reports to no one. Then we insert the employees directly reporting to the CEO. Then the employees reporting to those. Etc.

And that's called a topological sort.

A big benefit is that we hit three birds with one stone:
- We detect cycles
- We have the nodes in an convenient order to insert them in the database
- Since the algorithm for the topological sort takes as input an adjacency matrix (more on this later), we can easily detect the invalid case of a node having more than one outgoing edge (i.e. more than one manager, i.e. multiple roots).

From now one, I will use the graph of employees (where `Zoe` has two managers) as example since that's a possible input to our API and we need to detect this case.

## Topological sort

From Wikipedia:

> A topological sort or topological ordering of a directed graph is a linear ordering of its vertices such that for every directed edge uv from vertex u to vertex v, u comes before v in the ordering. For instance, the vertices of the graph may represent tasks to be performed, and the edges may represent constraints that one task must be performed before another; in this application, a topological ordering is just a valid sequence for the tasks

That's a mouthful but it's not too hard. 

A useful command line utility that's already on your (Unix) machine is `tsort`, which takes a list of edges as input, and outputs a topological sort. Here is the input in a text file (`people.txt`):

```
Jane Ellen
Angela Ellen
Zoe Jane
Zoe Angela
Bella Angela
Miranda Angela
```

> `tsort` uses a simple way of defining each edge `A -> B` on its own line with the syntax: `A B`. The order of the lines does not matter.

And here's the `tsort` output:

```sh
$ tsort < people.txt
Bella
Miranda
Zoe
Angela
Jane
Ellen
```

The first 3 elements are the ones with no incoming edge, the Software Engineers, since no one reports to them. Then come their respective managers, Angela and Jane. Finally comes their manager, `Ellen`.

So to insert all those people in our `people` SQL table, we go through that list in reverse order: We can first insert `Ellen`, then `Jane`, etc, until we finally insert `Bella`.

Also, `tsort` detects cycles, for example if we add the line: `Ellen Zoe` at the end of `people.txt`, we get:

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

So, how can we implement something like `tsort` for our problem at hand? That's where [Kahn's algorithm](https://en.wikipedia.org/wiki/Topological_sorting#Kahn's_algorithm) comes in to do exactly that: find cycles in the graph and output a topological sort.

*Note that that's not the only solution and there are ways to detect cycles without creating a topological sort, but this algorithm seems relatively unknown and does not come up often on the Internet, so let's discover how it works and implement it. I promise, it's not complex.*

## How to store the graph in memory

There are many ways to do so, and Kahn's algorithm does not dictate which one to use. 

We'll use an [adjacency matrix](https://en.wikipedia.org/wiki/Adjacency_matrix), because it's simple conceptually, maps well to Kahn's algorithm, and can be optimized if needed. 

It's just a 2D square table of size `n x n` (where `n` is the number of nodes), where the cell at row `i` and column `j` is 1 if there is an edge from the node `i` to the node `j`, and otherwise, `0`. 

The order of the nodes is arbitrary, I'll use the alphabetical order because again, it's simple to do:

```
Angela
Bella
Ellen
Jane
Miranda
Zoe
```

Here, `Angela` is the node `0` and `Zoe` is the node `5`.

Since there is an edge from `Zoe` to `Angela`, i.e. from the node `5` to the node `0`, the cell at the position `(5, 0)` is set to `1`.


The full adjacency matrix for the employee graph in the example above looks like:

<table>
  <tbody>
    <tr> <th></th>        <th>Angela</th>  <th>Bella</th>  <th>Ellen</th> <th>Jane</th>  <th>Miranda</th> <th>Zoe</th>  </tr>
    <tr> <td>Angela</td>  <td>0</td>       <td>0</td>      <td>1</td>     <td>0</td>     <td>0</td>       <td>0</td>    </tr>
    <tr> <td>Bella</td>   <td>1</td>       <td>0</td>      <td>0</td>     <td>0</td>     <td>0</td>       <td>0</td>    </tr>
    <tr> <td>Ellen</td>   <td>0</td>       <td>0</td>      <td>0</td>     <td>0</td>     <td>0</td>       <td>0</td>    </tr>
    <tr> <td>Jane</td>    <td>0</td>       <td>0</td>      <td>1</td>     <td>0</td>     <td>0</td>       <td>0</td>    </tr>
    <tr> <td>Miranda</td> <td>1</td>       <td>0</td>      <td>0</td>     <td>0</td>     <td>0</td>       <td>0</td>    </tr>
    <tr> <td>Zoe</td>     <td>1</td>       <td>0</td>      <td>0</td>     <td>1</td>     <td>0</td>       <td>0</td>    </tr>
  </tbody>
</table>

The way to read this table is:

- For a given row, all the `1`'s indicate outgoing edges
- For a given column, all the `1`'s indicate incoming edges
- If there is a `1` on the diagonal, it means there is an edge going out of a node and going to the same node.

There are a lot of zeroes in this table. Some may think this is horribly inefficient, which it is, but it really depends on number of nodes, i.e. the number of employees in the organization. 
But note that this adjacency matrix is a concept, it shows what information is present, but not how it is stored.

For this article, we will store it the naive way, in a 2D array. Here are two optimization ideas I considered but have not had time to experiment with:

- Make this a bitarray. We are already only storing zeroes and ones, so it maps perfectly to this format.
- Since there are a ton of zeroes (in the valid case, a regular employee's row only has one `1` and the CEO's row is only zeroes), it is very compressible. An easy way would be to use run-length encoding, meaning, instead of `0 0 0 0`, we just store the number of times the number occurs: `4 0`. Easy to implement, easy to understand. A row compresses to just a few bytes. And this size would be constant, whatever the size of the organization (i.e. number of employees) is.

Wikipedia lists others if you are interested, it's a well-known problem.


Alright, now that we know how our graph is represented, on to the algorithm.

## Kahn's algorithm

[Kahn's algorithm](https://en.wikipedia.org/wiki/Topological_sorting#Kahn's_algorithm) keeps track of nodes with no incoming edge, and mutates the graph (in our case the adjacency matrix), by removing one edge at a time, until there are no more edges, and builds a list of nodes in the right order, which is the output.

Here's the pseudo-code:

```
 1│ L ← Empty list that will contain the sorted elements
 2│ S ← Set of all nodes with no incoming edge
 3│ 
 4│ while S is not empty do
 5│     remove a node n from S
 6│     add n to L
 7│     for each node m with an edge e from n to m do
 8│         remove edge e from the graph
 9│         if m has no other incoming edges then
10│             insert m into S
11│ 
12│ if graph has edges then
13│     return error   (graph has at least one cycle)
14│ else 
15│     return L   (a topologically sorted order)
```

And in plain English:

`Line 1`: The result of this algorithm is the list of nodes in the desired order (topological). It starts empty, and we add nodes one-by one during the algorithm. We can simply use an array in our implementation.

`Line 2`: We first collect all nodes with no incoming edge. In terms of adjacency matrix, it means picking columns with only zeroes. The algorithm calls it a set, but we are free in our implementation to use whatever data structure we see fit. It just means a given node appears at most once in it. In our example, this set is: `[Zoe, Bella, Miranda]`. During the algorithm course, we will add further nodes to this set. Note that this is a working set, not the final result. Also, the order does not matter.

`Line 4`: Self-explanatory, we continue until the working set is empty and there is no more work to do.

`Line 5`: We first pick a node with no incoming edge (it does not matter which one). For example, `Zoe`, and remove it from `S`. `S` is now: `[Bella, Miranda]`.

`Line 6`: We add this node to the list of topologically sorted nodes, `L`. It now is: `[Zoe]`.

`Line 7`: We then inspect each node that `Zoe` has an edge to. That means `Jane` and `Angela`. In terms of adjacency matrix, we simply read `Zoe's` row, and inspect cells with a `1` in it.

`Line 8`: We remove such an edge, for example, `Zoe -> Jane`. In terms of adjacency matrix, it means setting the cell on the row `Zoe` and column `Jane` to `0`.

At this point, the graph looks like this:

![Employee hierarchy](kahns_algorithm_2.svg)

`Line 9`: If `Jane` does not have another incoming edge, we add it to the set of all nodes with no incoming edge. That's the case here, so `S` now looks like: `[Bella, Miranda, Jane]`.

We know loop to `Line 7` and handle the node `Angela` since `Jane` is taken care of.

`Line 7-10`: We are now handling the node `Angela`. We remove the edge `Zoe -> Angela`. We check whether the node `Angela` has incoming edges. It does, so we do **not** add it to `S`. 


The graph is now:

![Employee hierarchy](kahns_algorithm_2_1.svg)

We are now done with the `Line 7` for loop, so go back to `Line 5` and pick this time `Bella`. And so on.

The graph would now, to the algorithm, look like:

![Employee hierarchy](kahns_algorithm_2_2.svg)

<hr>

And here are the next steps in images:

1. ![Employee hierarchy](kahns_algorithm_2_3.svg)
1. ![Employee hierarchy](kahns_algorithm_2_4.svg)
1. ![Employee hierarchy](kahns_algorithm_2_5.svg)
1. ![Employee hierarchy](kahns_algorithm_2_6.svg)
1. ![Employee hierarchy](kahns_algorithm_2_7.svg)
1. ![Employee hierarchy](kahns_algorithm_2_8.svg)
1. ![Employee hierarchy](kahns_algorithm_2_9.svg)

<hr>

`Line 12-15`: Once the loop at `Line 4` is finished, we inspect our graph. If there are no more edges, we are done. If there is still an edge, it means there was a cycle in the graph, and we return an error.
Note that this algorithm is not capable by itself to point out which cycle there was exactly, only that there was one. That's because we mutated the graph by removing edges. If this information was important, we could keep track of which edges we removed in order, and re-add them back, or perhaps apply the algorithm to a copy of the graph (the adjacency matrix is trivial to clone).


This algorithm is loose concerning the order of some operations, for example, picking a node with no incoming edge, or in which order the nodes in `S` are stored. That gives room for an implementation to use certain data structures or orders that are faster, but in some cases we want the order to be always the same to solve ties in the stable way and to be reproducible. In order to do that, we simply use the alphabetical order. So in our example above, at `Line 5`, we picked `Zoe` out of `[Zoe, Bella, Miranda]`. Using this method, we would keep the working set `S` sorted alphabetically and pick `Bella` out of `[Bella, Miranda, Zoe]`.


## Implementation

I implemented this at the time in `Go`, but I will use for this article the lingua franca of the 2010s, Javascript.

*I don't write Javascript these days, I stopped many years ago, so apologies in advance if I am not using all the bells and whistles of 'Modern Javascript', or if the code is not quite idiomatic.*


First, we define our adjacency matrix and the list of nodes. This is the naive format. We would get the nodes and edges in some format, for example JSON, in the API, and build the adjacency matrix, which is trivial. Let's take the very first example, the (valid) tree  of employees:

```js
const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Jane", "Miranda", "Zoe"];
```
 
### Helpers

We need a helper function to check if a node has no incoming edge (`Line 9` in the algorithm):

```js
function hasNodeNoIncomingEdge(adjacencyMatrix, nodeIndex) {
  const column = nodeIndex;

  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    const cell = adjacencyMatrix[row][column];
    if (cell != 0) {
      return false;
    }
  }

  return true;
}
```

Then, using this helper, we can define a second helper to initially collect all the nodes with no incoming edge (`Line 2` in the algorithm):

```js
function getNodesWithNoIncomingEdge(adjacencyMatrix, nodes) {
  return nodes.filter((_, i) => hasNodeNoIncomingEdge(adjacencyMatrix, i));
}
```

We can try it:

```js
console.log(getNodesWithNoIncomingEdge(adjacencyMatrix, nodes));
```

And it outputs: 

```js
[ 'Bella', 'Miranda', 'Zoe' ]
```

We need one final helper, to determine if the graph has edges (`Line 12`), which is straightforward:

```js
function graphHasEdges(adjacencyMatrix) {
  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    for (let column = 0; column < adjacencyMatrix.length; column += 1) {
      if (adjacencyMatrix[row][column] == 1) return true;
    }
  }

  return false;
}
```


### The algorithm

We are finally ready to implement the algorithm. It's a straightforward, line by line, translation of the pseudo-code:

```js
function topologicalSort(adjacencyMatrix) {
  const L = [];
  const S = getNodesWithNoIncomingEdge(adjacencyMatrix, nodes);

  while (S.length > 0) {
    const node = S.pop();
    L.push(node);
    const nodeIndex = nodes.indexOf(node);

    for (let mIndex = 0; mIndex < nodes.length; mIndex += 1) {
      const hasEdgeFromNtoM = adjacencyMatrix[nodeIndex][mIndex];
      if (!hasEdgeFromNtoM) continue;

      adjacencyMatrix[nodeIndex][mIndex] = 0;

      if (hasNodeNoIncomingEdge(adjacencyMatrix, mIndex)) {
        const m = nodes[mIndex];
        S.push(m);
      }
    }
  }

  if (graphHasEdges(adjacencyMatrix)) {
    throw new Error("Graph has at least one cycle");
  }

  return L;
}
```


Let's try it:

```js
console.log(topologicalSort(adjacencyMatrix, nodes));
```

We get:

```js
[ 'Zoe', 'Jane', 'Miranda', 'Bella', 'Angela', 'Ellen' ]
```

Interestingly, it is not the same order as `tsort`, but it is indeed a valid topological ordering. That's because there are ties between some nodes and we do not resolve those ties the exact same way `tsort` does.

But in our specific case, we just want a valid insertion order in the database, and so this is enough.

### Inserting entries in the database

Now, we can produce the SQL code to insert our entries. We operate on a clone of the adjacency matrix for convenience because we later need to know what is the outgoing edge for a given node.

We handle the special case of the root first, which is the last element, and then we go through the topologically sorted list of employees in reverse order, and insert each one. We use a one liner to get the manager id by name when inserting to avoid many round trips to the database:

```js
const employeesTopologicallySorted = topologicalSort(structuredClone(adjacencyMatrix), nodes)

const root = employeesTopologicallySorted[employeesTopologicallySorted.length - 1];
console.log(`INSERT INTO people VALUES("${root}", NULL)`);

for (let i = employeesTopologicallySorted.length - 2; i >= 0; i -= 1) {
  const employee = employeesTopologicallySorted[i];
  const employeeIndex = nodes.indexOf(employee);

  const managerIndex = adjacencyMatrix[employeeIndex].indexOf(1);
  const manager = nodes[managerIndex];
  console.log(
    `INSERT INTO people SELECT "${employee}", rowid FROM people WHERE name = "${manager}" LIMIT 1;`,
  );
}
```

Which outputs:

```sql
INSERT INTO people VALUES("Ellen", NULL);
INSERT INTO people SELECT "Angela", rowid FROM people WHERE name = "Ellen" LIMIT 1;
INSERT INTO people SELECT "Bella", rowid FROM people WHERE name = "Angela" LIMIT 1;
INSERT INTO people SELECT "Miranda", rowid FROM people WHERE name = "Angela" LIMIT 1;
INSERT INTO people SELECT "Jane", rowid FROM people WHERE name = "Ellen" LIMIT 1;
INSERT INTO people SELECT "Zoe", rowid FROM people WHERE name = "Jane" LIMIT 1;
```

### Detecting cycles

As we said earlier, we get that for free, so let's check our implementation against this invalid example:

![Employee hierarchy with cycle](kahns_algorithm_4.svg)

We add the edge `Ellen -> Zoe` to create a cycle:

```js
const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 1], // => We change the last element of this row (Ellen's row, Zoe's column) from 0 to 1.
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Jane", "Miranda", "Zoe"];

const employeesTopologicallySorted = topologicalSort(structuredClone(adjacencyMatrix), nodes);
```

And we get an error as expected:

```sh
/home/pg/my-code/blog/kahns_algorithm.js:63
    throw new Error("Graph has at least one cycle");
    ^

Error: Graph has at least one cycle
```

### Detecting multiple roots


One thing that topological sorting does not do for us is to detect the case of multiple roots in the graph, for example:


![Employee hierarchy with multiple roots](kahns_algorithm_3.svg)

To do this, we simply scan the adjacency matrix and verify that there is only one row with only zeroes, that is, only one node that has no outgoing edges:

```js
function hasMultipleRoots(adjacencyMatrix) {
  let countOfRowsWithOnlyZeroes = 0;

  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    let rowHasOnlyZeroes = true;
    for (let column = 0; column < adjacencyMatrix.length; column += 1) {
      if (adjacencyMatrix[row][column] != 0) {
        rowHasOnlyZeroes = false;
        break;
      }
    }
    if (rowHasOnlyZeroes) countOfRowsWithOnlyZeroes += 1;
  }

  return countOfRowsWithOnlyZeroes > 1;
}
```

Let's try it with our invalid example from above:

```js
const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0, 0],
  [1, 0, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0, 0],
  [1, 0, 0, 0, 0, 0, 0],
  [0, 0, 0, 1, 0, 0, 0],
  [0, 0, 0, 0, 0, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Jane", "Miranda", "Zoe", "Kelly"];


console.log(hasMultipleRoots(adjacencyMatrix));
```

And we get: `true`. With our previous (valid) example, we get: `false`.

## Playing with the database

We can query each employee along with their manager name so:

```sql
SELECT a.name as employee_name, COALESCE(b.name, '') as manager_name FROM people a LEFT JOIN people b ON a.manager = b.rowid;
```

To query the manager (N+1) and the manager's manager (N+2) of an employee:

```sql
SELECT COALESCE(n_plus_1.name, ''), COALESCE(n_plus_2.name, '')
FROM people employee
LEFT JOIN people n_plus_1 ON employee.manager = n_plus_1.rowid
LEFT JOIN people n_plus_2 ON n_plus_1.manager = n_plus_2.rowid
WHERE employee.name = ?
```

We can also do this with hairy recursive Common Table Expression (CTE) but I'll leave that to the reader.

## Closing thoughts

Graphs and algorithms operating on them do not have to be complicated. Using an adjacency matrix and Kahn's algorithm, we can achieve a lot with little and it remains simple.

There are many ways to optimize the code in this article; the point was not to write the most efficient code, but to showcase in the clearest, simplest way possible to detect cycles and store a graph/tree in memory and in a database. 

If you want to play with the code here and try to make it faster, go at it!

## Addendum: the full code

```js
const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Jane", "Miranda", "Zoe"];

function hasNodeNoIncomingEdge(adjacencyMatrix, nodeIndex) {
  const column = nodeIndex;

  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    const cell = adjacencyMatrix[row][column];

    if (cell != 0) {
      return false;
    }
  }

  return true;
}

function getNodesWithNoIncomingEdge(adjacencyMatrix, nodes) {
  return nodes.filter((_, i) => hasNodeNoIncomingEdge(adjacencyMatrix, i));
}

function graphHasEdges(adjacencyMatrix) {
  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    for (let column = 0; column < adjacencyMatrix.length; column += 1) {
      if (adjacencyMatrix[row][column] == 1) return true;
    }
  }

  return false;
}

function topologicalSort(adjacencyMatrix) {
  const L = [];
  const S = getNodesWithNoIncomingEdge(adjacencyMatrix, nodes);

  while (S.length > 0) {
    const node = S.pop();
    L.push(node);
    const nodeIndex = nodes.indexOf(node);

    for (let mIndex = 0; mIndex < nodes.length; mIndex += 1) {
      const hasEdgeFromNtoM = adjacencyMatrix[nodeIndex][mIndex];
      if (!hasEdgeFromNtoM) continue;

      adjacencyMatrix[nodeIndex][mIndex] = 0;

      if (hasNodeNoIncomingEdge(adjacencyMatrix, mIndex)) {
        const m = nodes[mIndex];
        S.push(m);
      }
    }
  }

  if (graphHasEdges(adjacencyMatrix)) {
    throw new Error("Graph has at least one cycle");
  }

  return L;
}

function hasMultipleRoots(adjacencyMatrix) {
  let countOfRowsWithOnlyZeroes = 0;

  for (let row = 0; row < adjacencyMatrix.length; row += 1) {
    let rowHasOnlyZeroes = true;
    for (let column = 0; column < adjacencyMatrix.length; column += 1) {
      if (adjacencyMatrix[row][column] != 0) {
        rowHasOnlyZeroes = false;
        break;
      }
    }
    if (rowHasOnlyZeroes) countOfRowsWithOnlyZeroes += 1;
  }

  return countOfRowsWithOnlyZeroes > 1;
}

console.log(hasMultipleRoots(adjacencyMatrix));
const employeesTopologicallySorted = topologicalSort(structuredClone(adjacencyMatrix), nodes);
console.log(employeesTopologicallySorted);

const root = employeesTopologicallySorted[employeesTopologicallySorted.length - 1];
console.log(`INSERT INTO people VALUES("${root}", NULL)`);

for (let i = employeesTopologicallySorted.length - 2; i >= 0; i -= 1) {
  const employee = employeesTopologicallySorted[i];
  const employeeIndex = nodes.indexOf(employee);

  const managerIndex = adjacencyMatrix[employeeIndex].indexOf(1);
  const manager = nodes[managerIndex];
  console.log(
    `INSERT INTO people SELECT "${employee}", rowid FROM people WHERE name = "${manager}" LIMIT 1;`,
  );
}
```
