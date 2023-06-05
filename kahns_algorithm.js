const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Jane", "Miranda", "Zoe"];

function hasNodeNoIncomingEdge(adjacencyMatrix, nodes, nodeIndex) {
  const column = nodeIndex;

  for (let row = 0; row < nodes.length; row += 1) {
    const cell = adjacencyMatrix[row][column];

    if (cell != 0) {
      return false;
    }
  }

  return true;
}

function getNodesWithNoIncomingEdge(adjacencyMatrix, nodes) {
  return nodes.filter((_, i) =>
    hasNodeNoIncomingEdge(adjacencyMatrix, nodes, i)
  );
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

    for (let mIndex = 0; mIndex < nodes.length; mIndex++) {
      const hasEdgeFromNtoM = adjacencyMatrix[nodeIndex][mIndex];
      if (!hasEdgeFromNtoM) continue;

      adjacencyMatrix[nodeIndex][mIndex] = 0;

      if (hasNodeNoIncomingEdge(adjacencyMatrix, nodes, mIndex)) {
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

const employeesTopologicallySorted = topologicalSort(
  structuredClone(adjacencyMatrix),
  nodes,
);
console.log(employeesTopologicallySorted);

const root =
  employeesTopologicallySorted[employeesTopologicallySorted.length - 1];
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
