#ifndef KERNEL_H
#define KERNEL_H

#include <cstddef>
#include <cuda_runtime.h>
#include "params_kernel.cuh"

struct cudaGraphicsResource;
// -----------------------------------------------------------------------------
// VOLUME BOUNDARY SENSOR FACE ORDER
// -----------------------------------------------------------------------------
//
// Each face occupies one fixed stride inside the boundary mask:
//
//     maskIndex = face * faceStride + localPatchIndex
//
// Face-local axes:
//
//     -X / +X: u = Y, v = Z
//     -Y / +Y: u = X, v = Z
//     -Z / +Z: u = X, v = Y
// -----------------------------------------------------------------------------
enum VolumeBoundaryFaceId {
    VOLUME_BOUNDARY_NEG_X = 0,
    VOLUME_BOUNDARY_POS_X,
    VOLUME_BOUNDARY_NEG_Y,
    VOLUME_BOUNDARY_POS_Y,
    VOLUME_BOUNDARY_NEG_Z,
    VOLUME_BOUNDARY_POS_Z,

    VOLUME_BOUNDARY_FACE_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
    ///-----------------------------------------------------------------------------------------
    /// <CUDA HELPERS>
    ///-----------------------------------------------------------------------------------------
    void cudaInit(int argc, char** argv);
    void cudaGLInit(int argc, char** argv);

    void allocateArray(void** devPtr, size_t size);
    void freeArray(void* devPtr);
    void threadSync();

    void copyArrayToDevice(void* device, const void* host, int offset, int size);

    void registerGLBufferObject(unsigned int vbo, struct cudaGraphicsResource** cuda_vbo_resource);
    void unregisterGLBufferObject(struct cudaGraphicsResource* cuda_vbo_resource);
    void* mapGLBufferObject(struct cudaGraphicsResource** cuda_vbo_resource);
    void unmapGLBufferObject(struct cudaGraphicsResource* cuda_vbo_resource);
    void copyArrayFromDevice(void* host, const void* device,
        struct cudaGraphicsResource** cuda_vbo_resource, int size);
    ///-----------------------------------------------------------------------------------------
    /// </CUDA HELPERS>
    ///-----------------------------------------------------------------------------------------




    ///-----------------------------------------------------------------------------------------
    /// <PARTICLE SYSTEM CUDA SOLVER>
    ///-----------------------------------------------------------------------------------------
    void setParameters(SimParams* hostParams);
    void integrateSystem(float* pos, float* vel, float* acc, float deltaTime, unsigned int numParticles);
    void forcesKernel(float* pos, float* acc, int numParticles);
    void calcHash(unsigned int* gridParticleHash, unsigned int* gridParticleIndex, float* pos, int numParticles);

    void reorderDataAndFindCellStart(unsigned int* cellStart, unsigned int* cellEnd,
        float* sortedPos, float* sortedVel,
        unsigned int* gridParticleHash, unsigned int* gridParticleIndex,
        float* oldPos, float* oldVel,
        unsigned int numParticles, unsigned int numCells);

    void collide(float* newVel, float* sortedPos, float* sortedVel, unsigned int* gridParticleIndex,
        unsigned int* cellStart, unsigned int* cellEnd,
        unsigned int numParticles, unsigned int numCells);

    void sortParticles(unsigned int* dGridParticleHash, unsigned int* dGridParticleIndex, unsigned int numParticles);
    ///-----------------------------------------------------------------------------------------
    /// </PARTICLE SYSTEM CUDA SOLVER>
    ///-----------------------------------------------------------------------------------------


    ///-----------------------------------------------------------------------------------------
    /// <VOLUME RENDERING>
    ///-----------------------------------------------------------------------------------------
    void kernelLauncher(
        uchar4* d_out,
        float* d_vol,
        int w, int h,
        int3 volSize,
        int method,
        float zs,
        float theta,
        float phi,
        float threshold,
        float dist,
        float tintR,
        float tintG,
        float tintB
    );

    void kernelOverlayLauncher(
        uchar4* d_out,
        float* d_vol,
        int w, int h,
        int3 volSize,
        int method,
        float zs,
        float theta,
        float phi,
        float threshold,
        float dist,
        float tintR,
        float tintG,
        float tintB,
        float overlayAlpha
    );

    void volumeKernelLauncher(
        float* d_vol,
        int3 volSize,
        int id,
        float4 param,
        float3 offset,
        float3 basisX,
        float3 basisY,
        float3 basisZ
    );

    void mirroredVolumeKernelLauncher(
        float* d_vol,
        int3 volSize,
        int id,
        float4 param,
        float3 offset,
        float3 basisX,
        float3 basisY,
        float3 basisZ,
        float3 mirrorNormal
    );

    void clearVolumeKernelLauncher(
        float* d_vol,
        int3 volSize,
        float clearValue
    );

    void composeVolumePrimitiveLauncher(
        float* d_base,
        float* d_out,
        int3 volSize,
        int id,
        float4 param,
        float3 offset,
        float3 basisX,
        float3 basisY,
        float3 basisZ,
        int op
    );

    void composeVolumeFieldsLauncher(
        const float* d_base,
        const float* d_brush,
        float* d_out,
        int3 volSize,
        int op
    );

    void transformVolumeFieldLauncher(
        const float* d_source,
        float* d_destination,
        int3 volSize,
        float3 scale,
        float3 offset,
        float3 basisX,
        float3 basisY,
        float3 basisZ
    );

    

    // -------------------------------------------------------------------------
    // VOLUME BOUNDARY SENSOR
    // -------------------------------------------------------------------------

    // Returns the number of mask entries reserved for each face.
    //
    // The stride is the largest face area:
    //
    //     max(X * Y, X * Z, Y * Z)
    //
    // This gives all six faces a uniform layout even for a non-cubic volume.
    uint getVolumeBoundaryFaceStride(int3 volSize);
    size_t getVolumeBoundaryMaskBytes(int3 volSize);

    void classifyVolumeBoundaryLauncher(
        const float* d_volume,
        uchar* d_boundaryMask,
        uint* d_unsafeCount,
        uint* d_insideSampleCount,
        int3 volSize,
        float isoValue,
        float safetyBand
    );
    ///-----------------------------------------------------------------------------------------
    /// </VOLUME RENDERING>
    ///-----------------------------------------------------------------------------------------


    ///-----------------------------------------------------------------------------------------
    /// <MARCHING CUBES TABLE MANAGEMENT>
    ///-----------------------------------------------------------------------------------------


    void allocateMarchingCubesTables(
        uint** d_edgeTable,
        uint** d_triTable,
        uint** d_numVertsTable
    );

    void freeMarchingCubesTables(
        uint* d_edgeTable,
        uint* d_triTable,
        uint* d_numVertsTable
    );


    ///-----------------------------------------------------------------------------------------
    /// </MARCHING CUBES TABLE MANAGEMENT>
    ///-----------------------------------------------------------------------------------------

    ///-----------------------------------------------------------------------------------------
    /// <MARCHING CUBES KERNEL LAUNCHERS>
    ///-----------------------------------------------------------------------------------------
    void setMarchingCubesParameters(
        MarchingCubesParams* hostParams
    );

    void launchMCClassifyVoxels(
        dim3 grid,
        dim3 threads,
        uint* voxelVerts,
        uint* voxelOccupied,
        float* volume,
        uint* numVertsTable,
        uint3 gridSize,
        uint3 gridSizeShift,
        uint3 gridSizeMask,
        uint numVoxels,
        float3 voxelSize,
        float isoValue
    );

    void launchMCCompactVoxels(
        dim3 grid,
        dim3 threads,
        uint* compactedVoxelArray,
        uint* voxelOccupied,
        uint* voxelOccupiedScan,
        uint numVoxels
    );

    void launchMCGenerateTriangles(
        dim3 grid,
        dim3 threads,
        float4* pos,
        float4* norm,
        uint* compactedVoxelArray,
        uint* numVertsScanned,
        float* volume,
        uint* numVertsTable,
        uint* triTable,
        uint3 gridSize,
        uint3 gridSizeShift,
        uint3 gridSizeMask,
        float3 voxelSize,
        float isoValue,
        uint activeVoxels,
        uint maxVerts
    );

    void mcThrustScanWrapper(
        uint* output,
        uint* input,
        uint numElements
    );
    ///-----------------------------------------------------------------------------------------
    /// </MARCHING CUBES KERNEL LAUNCHERS>
    ///-----------------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif

