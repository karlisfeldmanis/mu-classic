#ifndef MU_PATH_FINDER_HPP
#define MU_PATH_FINDER_HPP

// A* pathfinder for 256x256 terrain grid.
// Port of OpenMU's PathFinder.cs + ScopedGridNetwork.cs.
// Pure algorithm — no server/client dependencies.

#include <cstdint>
#include <vector>

#ifndef MU_GRID_POINT_DEFINED
#define MU_GRID_POINT_DEFINED
struct GridPoint {
  uint8_t x = 0, y = 0;
  bool operator==(const GridPoint &o) const { return x == o.x && y == o.y; }
  bool operator!=(const GridPoint &o) const { return !(*this == o); }
};
#endif

class PathFinder {
public:
  // Find path from start to end on the terrain attribute grid.
  // terrainAttribs: flat 256*256 array (row = y*256+x). Walkable = !(attr &
  // 0x04). canEnterSafeZone: if false, cells with (attr & 0x01) are blocked.
  // occupancyGrid: optional 256*256 bool grid; true = cell occupied by another
  // monster. maxSteps: path truncated to this length (OpenMU Walker limit =
  // 16). searchLimit: max nodes expanded before giving up (OpenMU = 500).
  // Returns: grid points from start (exclusive) to end (inclusive), or empty if
  // no path.
  std::vector<GridPoint> FindPath(GridPoint start, GridPoint end,
                                  const uint8_t *terrainAttribs,
                                  int maxSteps = 16, int searchLimit = 500,
                                  bool canEnterSafeZone = false,
                                  const bool *occupancyGrid = nullptr) const;

  // Chebyshev distance (max of |dx|, |dy|) — used for all range checks
  static int ChebyshevDist(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by);
  static int ChebyshevDist(GridPoint a, GridPoint b);

  // Terrain attribute flags (from _define.h)
  static constexpr uint8_t TW_SAFEZONE = 0x01;
  static constexpr uint8_t TW_NOMOVE = 0x04;
  static constexpr uint8_t TW_NOGROUND = 0x08;
  static constexpr int TERRAIN_SIZE = 256;
};

#endif // MU_PATH_FINDER_HPP
