#ifndef PARAMS_KERNEL_CUH
#define PARAMS_KERNEL_CUH

#include <cuda_runtime.h>
#include <vector_types.h>

typedef unsigned int uint;
typedef unsigned char uchar;

#define G 1.0f
#define EPS2 1e-4f
#define USE_TEX 0
#define SAMPLE_VOLUME 1

// Using shared to store computed vertices and normals during triangle generation
// improves performance
#define USE_SHARED 1

// The number of threads to use for triangle generation (limited by shared memory size)
#define NTHREADS 32

#define SKIP_EMPTY_VOXELS 1

#ifndef USE_TEX
#define USE_TEX 0
#endif

#if USE_TEX
#define FETCH(t, i) tex1Dfetch(t##Tex, (i))
#else
#define FETCH(t, i) ((t)[(i)])
#endif

struct SimParams {
	uint numCells;
	uint numBodies;
	uint maxParticlesPerCell;
    
    uint3 gridSize;
    
    float3 gravity;
    float3 cellSize;
    float3 worldOrigin;
    
    float globalDamping ;
    float particleRadius;
    
    float shear;
    float spring;
    float damping;
    float boundary;
    float attraction;
    float boundaryDamping;
};

struct MarchingCubesParams {
	uint3 volumeSize;
	float3 voxelSize;
    
	float isoValue;
    
	uint numCells;
	uint maxVerts;
};


#endif