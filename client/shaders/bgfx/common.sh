// Common shader includes for MU Remaster BGFX shaders.
// Shared constants and utility functions.

#ifndef MU_COMMON_SH
#define MU_COMMON_SH

// Maximum point lights per draw call
#define MAX_POINT_LIGHTS 64

// Maximum bones for GPU skeletal animation
#define MAX_BONES 48

// Chrome rendering modes (matches client-side enum)
#define CHROME_OFF    0.0
#define CHROME_MODE1  1.0
#define CHROME_MODE2  2.0
#define CHROME_METAL  3.0
#define CHROME_MODE4  4.0

#endif // MU_COMMON_SH
