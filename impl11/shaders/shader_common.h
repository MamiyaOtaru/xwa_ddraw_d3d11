// Scale factor used to reconstruct metric 3D for VR and other uses
#define METRIC_SCALE_FACTOR 25.0
//#define METRIC_SCALE_FACTOR 50.0

// This is the limit, in meters, when we start fading out effects like SSAO and SSDO:
#define INFINITY_Z0 15000
// This is the limit, in meters, when the SSAO/SSDO effects are completely faded out
#define INFINITY_Z1 20000
// This is simply INFINITY_Z1 - INFINITY_Z0: It's used to fade the effect
#define INFINITY_FADEOUT_RANGE 5000

// Dynamic Cockpit: Maximum Number of DC elements per texture
#define MAX_DC_COORDS 12

#define DEFAULT_GLOSSINESS 0.08
#define DEFAULT_SPEC_INT   0.35
