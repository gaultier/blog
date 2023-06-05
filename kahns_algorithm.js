const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [1, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Miranda", "Zoe"];

function hasNodeNoIncomingEdge(adjacencyMatrix, nodes, nodeIndex) {
  const column = nodeIndex;

  for (row = 0; row < nodes.length; row += 1) {
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

console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 0));
console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 1));
console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 2));
console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 3));
console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 4));
console.log(hasNodeNoIncomingEdge(adjacencyMatrix, nodes, 5));

console.log(getNodesWithNoIncomingEdge(adjacencyMatrix, nodes));

function topologicalSort(adjacencyMatrix) {
  const L = [];
  const S = getNodesWithNoIncomingEdge(adjacencyMatrix);
}
