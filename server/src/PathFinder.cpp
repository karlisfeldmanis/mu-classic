// A* pathfinder — port of OpenMU PathFinder.cs + ScopedGridNetwork.cs.
// Operates on a scoped segment (8-16 cells) of the 256x256 terrain grid.
// Uses binary min-heap open list, Chebyshev heuristic * 2, search limit 500.

#include "PathFinder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// ── Internal types ──────────────────────────────────────────────────────────

namespace {

enum class NodeStatus : uint8_t { Undefined = 0, Open = 1, Closed = 2 };

struct Node {
  uint8_t x = 0, y = 0;
  int costUntilNow = 0;     // g: cost from start to here
  int predictedTotal = 0;   // f: g + h
  int parentIdx = -1;       // index into node pool (-1 = start sentinel)
  NodeStatus status = NodeStatus::Undefined;
};

// Direction offsets: N, E, S, W, NE, SE, SW, NW (matches OpenMU)
static constexpr int8_t DIR_DX[8] = {0, 1, 0, -1, 1, 1, -1, -1};
static constexpr int8_t DIR_DY[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

} // namespace

// ── Binary min-heap for open list ───────────────────────────────────────────

namespace {

struct MinHeap {
  std::vector<int> heap; // indices into node pool

  void clear() { heap.clear(); }
  bool empty() const { return heap.empty(); }
  int size() const { return (int)heap.size(); }

  void push(int nodeIdx, const std::vector<Node> &nodes) {
    heap.push_back(nodeIdx);
    siftUp((int)heap.size() - 1, nodes);
  }

  int pop(const std::vector<Node> &nodes) {
    int top = heap[0];
    heap[0] = heap.back();
    heap.pop_back();
    if (!heap.empty())
      siftDown(0, nodes);
    return top;
  }

private:
  void siftUp(int i, const std::vector<Node> &nodes) {
    while (i > 0) {
      int parent = (i - 1) / 2;
      if (nodes[heap[i]].predictedTotal < nodes[heap[parent]].predictedTotal) {
        std::swap(heap[i], heap[parent]);
        i = parent;
      } else
        break;
    }
  }

  void siftDown(int i, const std::vector<Node> &nodes) {
    int n = (int)heap.size();
    while (true) {
      int smallest = i;
      int left = 2 * i + 1;
      int right = 2 * i + 2;
      if (left < n &&
          nodes[heap[left]].predictedTotal <
              nodes[heap[smallest]].predictedTotal)
        smallest = left;
      if (right < n &&
          nodes[heap[right]].predictedTotal <
              nodes[heap[smallest]].predictedTotal)
        smallest = right;
      if (smallest != i) {
        std::swap(heap[i], heap[smallest]);
        i = smallest;
      } else
        break;
    }
  }
};

} // namespace

// ── Chebyshev distance ──────────────────────────────────────────────────────

int PathFinder::ChebyshevDist(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by) {
  return std::max(std::abs((int)ax - (int)bx), std::abs((int)ay - (int)by));
}

int PathFinder::ChebyshevDist(GridPoint a, GridPoint b) {
  return ChebyshevDist(a.x, a.y, b.x, b.y);
}

// ── A* implementation ───────────────────────────────────────────────────────

std::vector<GridPoint>
PathFinder::FindPath(GridPoint start, GridPoint end,
                     const uint8_t *terrainAttribs, int maxSteps,
                     int searchLimit, bool canEnterSafeZone,
                     const bool *occupancyGrid) const {

  // No terrain data — cannot pathfind
  if (!terrainAttribs)
    return {};

  // Trivial: already at destination
  if (start == end)
    return {};

  // ScopedGridNetwork: compute bounding segment
  int diffX = std::abs((int)end.x - (int)start.x);
  int diffY = std::abs((int)end.y - (int)start.y);

  static constexpr int MAX_SEGMENT = 16;
  static constexpr int MIN_SEGMENT = 8;

  // Reject if too far apart for scoped search
  if (diffX > MAX_SEGMENT || diffY > MAX_SEGMENT)
    return {};

  // Determine actual segment side length (power of 2: 8 or 16)
  int segSide = MIN_SEGMENT;
  while ((diffX > segSide - 1 || diffY > segSide - 1) && segSide < MAX_SEGMENT)
    segSide *= 2;

  // Segment offset: center on midpoint of start/end, clamped to grid bounds
  int avgX = (int)start.x / 2 + (int)end.x / 2;
  int avgY = (int)start.y / 2 + (int)end.y / 2;

  int offX = std::max(0, avgX - segSide / 2);
  offX = std::min(offX, TERRAIN_SIZE - segSide);
  int offY = std::max(0, avgY - segSide / 2);
  offY = std::min(offY, TERRAIN_SIZE - segSide);

  // Bit shift for coordinate → index mapping
  int bitsPerCoord = 0;
  {
    int tmp = segSide;
    while (tmp > 1) {
      bitsPerCoord++;
      tmp >>= 1;
    }
  }

  int totalNodes = segSide * segSide;

  // Node pool (scoped segment only — max 256 nodes)
  std::vector<Node> nodes(totalNodes);

  // Lambda: grid (x,y) → node pool index, or -1 if out of scope
  auto nodeIndex = [&](int x, int y) -> int {
    int lx = x - offX;
    int ly = y - offY;
    if (lx < 0 || ly < 0 || lx >= segSide || ly >= segSide)
      return -1;
    return (ly << bitsPerCoord) + lx;
  };

  // Lambda: check walkability for a terrain cell (caller ensures in-bounds)
  auto isWalkable = [&](uint8_t x, uint8_t y) -> bool {
    uint8_t attr = terrainAttribs[(int)y * TERRAIN_SIZE + (int)x];
    if (attr & (TW_NOMOVE | TW_NOGROUND))
      return false;
    if (!canEnterSafeZone && (attr & TW_SAFEZONE))
      return false;
    if (occupancyGrid && occupancyGrid[y * TERRAIN_SIZE + x])
      return false;
    return true;
  };

  // Chebyshev heuristic with multiplier 2 (matches OpenMU HeuristicEstimate=2)
  auto heuristic = [&](uint8_t x, uint8_t y) -> int {
    return 2 * std::max(std::abs((int)x - (int)end.x),
                        std::abs((int)y - (int)end.y));
  };

  // Initialize start node
  int startIdx = nodeIndex(start.x, start.y);
  if (startIdx < 0)
    return {};
  nodes[startIdx].x = start.x;
  nodes[startIdx].y = start.y;
  nodes[startIdx].costUntilNow = 0;
  nodes[startIdx].predictedTotal = 2; // OpenMU: startNode.PredictedTotalCost=2
  nodes[startIdx].parentIdx = startIdx; // self-parent = start sentinel
  nodes[startIdx].status = NodeStatus::Open;

  MinHeap openList;
  openList.push(startIdx, nodes);

  int closedCount = 0;
  bool pathFound = false;

  while (!openList.empty()) {
    int curIdx = openList.pop(nodes);
    Node &cur = nodes[curIdx];

    // Skip already-closed (duplicate in heap)
    if (cur.status == NodeStatus::Closed)
      continue;

    // Reached destination?
    if (cur.x == end.x && cur.y == end.y) {
      cur.status = NodeStatus::Closed;
      pathFound = true;
      break;
    }

    // Search limit exceeded?
    if (closedCount > searchLimit)
      break;

    // Expand neighbors (8 directions)
    for (int d = 0; d < 8; d++) {
      int nx = (int)cur.x + DIR_DX[d];
      int ny = (int)cur.y + DIR_DY[d];

      if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
        continue;

      uint8_t ux = (uint8_t)nx;
      uint8_t uy = (uint8_t)ny;

      if (!isWalkable(ux, uy))
        continue;

      int nIdx = nodeIndex(nx, ny);
      if (nIdx < 0)
        continue;

      Node &neighbor = nodes[nIdx];
      if (neighbor.status == NodeStatus::Closed)
        continue;

      // Cost: OpenMU uses grid value as cost; our walkable cells cost 1
      int newG = cur.costUntilNow + 1;

      if (neighbor.status == NodeStatus::Open && neighbor.costUntilNow <= newG)
        continue; // Existing path is cheaper

      // Update neighbor
      neighbor.x = ux;
      neighbor.y = uy;
      neighbor.costUntilNow = newG;
      neighbor.predictedTotal = newG + heuristic(ux, uy);
      neighbor.parentIdx = curIdx;
      neighbor.status = NodeStatus::Open;
      openList.push(nIdx, nodes);
    }

    cur.status = NodeStatus::Closed;
    closedCount++;
  }

  if (!pathFound)
    return {};

  // Reconstruct path (end → start), then reverse
  std::vector<GridPoint> path;
  int idx = nodeIndex(end.x, end.y);
  while (idx >= 0 && nodes[idx].parentIdx != idx) {
    path.push_back({nodes[idx].x, nodes[idx].y});
    idx = nodes[idx].parentIdx;
  }

  std::reverse(path.begin(), path.end());

  // Truncate to maxSteps (OpenMU: Walker limit = 16)
  if ((int)path.size() > maxSteps)
    path.resize(maxSteps);

  return path;
}
