const adjacencyMatrix = [
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [0, 0, 0, 0, 0, 0],
  [0, 0, 1, 0, 0, 0],
  [1, 0, 0, 0, 0, 0],
  [1, 0, 0, 1, 0, 0],
];

const nodes = ["Angela", "Bella", "Ellen", "Miranda", "Zoe"];

function getNodesWithNoIncomingEdge(adjacencyMatrix, nodes) {
  const result = [];

  for (column = 0; column < nodes.length; column += 1) {
    let columnHasOnlyZeroes = true;

    for (row = 0; row < nodes.length; row += 1) {
      const cell = adjacencyMatrix[row][column];

      if (cell != 0) {
        columnHasOnlyZeroes = false;
        break;
      }
    }

    if (columnHasOnlyZeroes) {
      const node = nodes[column];
      result.push(node);
    }
  }

  return result;
}

function hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, nodeIndex) {
  const column = nodeIndex;

  for (row = 0; row < nodes.length; row += 1) {
    const cell = adjacencyMatrix[row][column];

    if (cell != 0) {
      return false;
    }
  }

  return true;
}

console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 0));
console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 1));
console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 2));
console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 3));
console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 4));
console.log(hasNodeNoOtherIncomingEdge(adjacencyMatrix, nodes, 5));

function topologicalSort(adjacencyMatrix) {
  const L = [];
  const S = getNodesWithNoIncomingEdge(adjacencyMatrix);
}
