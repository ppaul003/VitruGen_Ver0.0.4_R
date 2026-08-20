#include <GL/freeglut.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <helper_cuda.h>
#include <helper_cuda_gl.h>
#include <cuda_runtime_api.h>
#include <cuda.h>

#include <helper_functions.h>
#include <helper_math.h>

#include <thrust/device_ptr.h>
#include <thrust/scan.h>
#include <thrust/sort.h>

#include <thrust/for_each.h>
#include <thrust/iterator/zip_iterator.h>
#include <thrust/sort.h>

#include "tables.h"
#include "kernel.h"
#include "kernel_impl.cuh"

#define TX_2D 32
#define TY_2D 32

extern "C" {
	void cudaInit(int argc, char** argv) {
		int devID;

		devID = findCudaDevice(argc, (const char**)argv);

		if (devID < 0) {

			printf("No CUDA Capable devices found, exiting...\n");
			exit(EXIT_SUCCESS);
		}
	}
	void cudaGLInit(int argc, char** argv) {
		findCudaGLDevice(argc, (const char**)argv);
	}

	void allocateArray(void** devPtr, size_t size) {
		cudaMalloc(devPtr, size);
	}

	void freeArray(void* devPtr) {
		cudaFree(devPtr);
	}

	void threadSync() {
		cudaDeviceSynchronize();
	}

	void copyArrayToDevice(void* device, const void* host, int offset, int size) {
		cudaMemcpy((char*)device + offset, host, size, cudaMemcpyHostToDevice);
	}

	void registerGLBufferObject(uint vbo, struct cudaGraphicsResource** cuda_vbo_resource) {
		cudaGraphicsGLRegisterBuffer(cuda_vbo_resource, vbo, cudaGraphicsMapFlagsNone);
	}
	void unregisterGLBufferObject(struct cudaGraphicsResource* cuda_vbo_resource) {
		cudaGraphicsUnregisterResource(cuda_vbo_resource);
	}

	void* mapGLBufferObject(struct cudaGraphicsResource** cuda_vbo_resource) {

		void* ptr;
		cudaGraphicsMapResources(1, cuda_vbo_resource, 0);
		size_t num_bytes;
		cudaGraphicsResourceGetMappedPointer((void**)&ptr, &num_bytes, *cuda_vbo_resource);
		return ptr;
	}
	void unmapGLBufferObject(struct cudaGraphicsResource* cuda_vbo_resource) {
		cudaGraphicsUnmapResources(1, &cuda_vbo_resource, 0);
	}

	void copyArrayFromDevice(void* host, const void* device, struct cudaGraphicsResource** cuda_vbo_resource, int size) {

		if (cuda_vbo_resource && *cuda_vbo_resource) {
			device = mapGLBufferObject(cuda_vbo_resource);
		}

		cudaMemcpy(host, device, size, cudaMemcpyDeviceToHost);

		if (cuda_vbo_resource && *cuda_vbo_resource) {
			unmapGLBufferObject(*cuda_vbo_resource);
		}
	}

	void setParameters(SimParams* hostParams) {
		cudaMemcpyToSymbol(cSimParams, hostParams, sizeof(SimParams));
	}

	uint iDivUp(uint a, uint b) {
		return (a % b != 0) ? (a / b + 1) : (a / b);
	}
	int divUpInt(int a, int b) {
		return (a + b - 1) / b;
	}

	void computeGridSize(uint n, uint blockSize, uint& numBlocks, uint& numThreads) {
		numThreads = min(blockSize, n);
		numBlocks = iDivUp(n, numThreads);
	}
	void integrateSystem(float* pos, float* vel, float* acc, float deltaTime, uint numParticles) {

		thrust::device_ptr<float4> d_pos4((float4*)pos);
		thrust::device_ptr<float4> d_vel4((float4*)vel);
		thrust::device_ptr<float4> d_acc4((float4*)acc);

		thrust::for_each(
			thrust::make_zip_iterator(thrust::make_tuple(d_pos4, d_vel4, d_acc4)),
			thrust::make_zip_iterator(thrust::make_tuple(d_pos4 + numParticles, d_vel4 + numParticles, d_acc4 + numParticles)),
			integrate_functor(deltaTime));
	}

	void forcesKernel(float* pos, float* acc, int numParticles) {
		uint numThreads = min((uint)256, numParticles);
		uint numBlocks = iDivUp(numParticles, numThreads);

		size_t smSz = numThreads * sizeof(float4);

		calculate_forces << <numBlocks, numThreads, smSz >> > ((float4*)pos, (float4*)acc, numParticles);
	}
	void calcHash(uint* gridParticleHash, uint* gridParticleIndex, float* pos, int numParticles) {

		uint numThreads, numBlocks;
		computeGridSize(numParticles, 256, numBlocks, numThreads);

		calcHashD << <numBlocks, numThreads >> > (gridParticleHash, gridParticleIndex, (float4*)pos, numParticles);
	}

	void reorderDataAndFindCellStart(uint* cellStart, uint* cellEnd, float* sortedPos, float* sortedVel, uint* gridParticleHash, uint* gridParticleIndex,
		float* oldPos, float* oldVel, uint numParticles, uint numCells) {

		uint numThreads, numBlocks;
		computeGridSize(numParticles, 256, numBlocks, numThreads);

		// set all cells to empty
		cudaMemset(cellStart, 0xffffffff, numCells * sizeof(uint));

#if USE_TEX
		cudaBindTexture(0, oldPosTex, oldPos, numParticles * sizeof(float4));
		cudaBindTexture(0, oldVelTex, oldVel, numParticles * sizeof(float4));
#endif

		uint smemSize = sizeof(uint) * (numThreads + 1);
		reorderDataAndFindCellStartD << <numBlocks, numThreads, smemSize >> > (
			cellStart, cellEnd,
			(float4*)sortedPos,
			(float4*)sortedVel,
			gridParticleHash, 
			gridParticleIndex,
			(float4*)oldPos,
			(float4*)oldVel,
			numParticles
			);

#if USE_TEX
		cudaUnbindTexture(oldPosTex);
		cudaUnbindTexture(oldVelTex);
#endif
	}

	void collide(float* newVel, float* sortedPos, float* sortedVel, uint* gridParticleIndex,
		uint* cellStart, uint* cellEnd, uint numParticles, uint numCells) {

#if USE_TEX
		cudaBindTexture(0, oldPosTex, sortedPos, numParticles * sizeof(float4));
		cudaBindTexture(0, oldVelTex, sortedVel, numParticles * sizeof(float4));
		cudaBindTexture(0, cellStartTex, cellStart, numCells * sizeof(uint));
		cudaBindTexture(0, cellEndTex, cellEnd, numCells * sizeof(uint));
#endif

		uint numThreads, numBlocks;
		computeGridSize(numParticles, 64, numBlocks, numThreads);

		collideD << <numBlocks, numThreads >> > (
			(float4*)newVel,
			(float4*)sortedPos,
			(float4*)sortedVel,
			gridParticleIndex,
			cellStart,
			cellEnd,
			numParticles
			);

#if USE_TEX
		cudaUnbindTexture(oldPosTex);
		cudaUnbindTexture(oldVelTex);
		cudaUnbindTexture(cellStartTex);
		cudaUnbindTexture(cellEndTex);
#endif
	}

	void sortParticles(uint* dGridParticleHash, uint* dGridParticleIndex, uint numParticles) {
		thrust::sort_by_key(
			thrust::device_ptr<uint>(dGridParticleHash),
			thrust::device_ptr<uint>(dGridParticleHash + numParticles),
			thrust::device_ptr<uint>(dGridParticleIndex));
	}

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
		float tintB) {

		dim3 blockSize(TX_2D, TY_2D);
		dim3 gridSize(divUp(w, TX_2D), divUp(h, TY_2D));

		renderKernel << <gridSize, blockSize >> > (
			d_out, d_vol,
			w, h, volSize, method,
			zs, theta, phi,
			threshold, dist,
			tintR, tintG, tintB
			);

		getLastCudaError("renderKernel failed");
	}

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
		float overlayAlpha) {

		dim3 blockSize(TX_2D, TY_2D);
		dim3 gridSize(divUp(w, TX_2D), divUp(h, TY_2D));

		renderKernelOverlay << <gridSize, blockSize >> > (
			d_out, d_vol, w, h, volSize, method,
			zs, theta, phi, threshold, dist,
			tintR, tintG, tintB, overlayAlpha
			);

		getLastCudaError("renderKernelOverlay failed");
	}

	void volumeKernelLauncher(
		float* d_vol, int3 volSize,
		int id, float4 param, float3 offset,
		float3 basisX, float3 basisY, float3 basisZ) {

		if (!d_vol) return;
		if (volSize.x <= 0 || volSize.y <= 0 || volSize.z <= 0) return;

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z
		);

		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z)
		);

		volumeKernel << <gridSize, blockSize >> > (
			d_vol, volSize,
			id, param, offset,
			basisX, basisY, basisZ
			);

		getLastCudaError("volumeKernel failed");
	}

	void mirroredVolumeKernelLauncher(
		float* d_vol, int3 volSize,
		int id, float4 param, float3 offset,
		float3 basisX, float3 basisY, float3 basisZ,
		float3 mirrorNormal) {
		if (!d_vol) return;
		if (volSize.x <= 0 || volSize.y <= 0 || volSize.z <= 0) return;
		const float normalLength = sqrtf(
			mirrorNormal.x * mirrorNormal.x +
			mirrorNormal.y * mirrorNormal.y +
			mirrorNormal.z * mirrorNormal.z);
		if (normalLength <= 1.0e-6f) return;
		mirrorNormal.x /= normalLength;
		mirrorNormal.y /= normalLength;
		mirrorNormal.z /= normalLength;

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z);
		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z));
		mirroredVolumeKernel<<<gridSize, blockSize>>>(
			d_vol, volSize, id, param, offset,
			basisX, basisY, basisZ, mirrorNormal);
		getLastCudaError("mirroredVolumeKernel failed");
	}

	uint getVolumeBoundaryFaceStride(int3 volSize) {
		if (volSize.x <= 0 || volSize.y <= 0 || volSize.z <= 0)
			return 0;

		const uint xy =
			static_cast<uint>(volSize.x) * static_cast<uint>(volSize.y);
		const uint xz =
			static_cast<uint>(volSize.x) * static_cast<uint>(volSize.z);
		const uint yz =
			static_cast<uint>(volSize.y) * static_cast<uint>(volSize.z);

		uint largest = xy;

		if (xz > largest) largest = xz;
		if (yz > largest) largest = yz;

		return largest;
	}

	size_t getVolumeBoundaryMaskBytes(int3 volSize) {

		const uint faceStride =
			getVolumeBoundaryFaceStride(volSize);

		if (faceStride == 0) return 0;

		return static_cast<size_t>(VOLUME_BOUNDARY_FACE_COUNT) *
			static_cast<size_t>(faceStride) * sizeof(uchar);
	}

	void clearVolumeKernelLauncher(
		float* d_vol,
		int3 volSize,
		float clearValue) {

		if (!d_vol) return;

		if (volSize.x <= 0 ||
			volSize.y <= 0 ||
			volSize.z <= 0) {
			return;
		}

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z
		);

		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z)
		);

		clearVolumeKernel << <gridSize, blockSize >> > (
			d_vol,
			volSize,
			clearValue
			);

		getLastCudaError("clearVolumeKernel failed");
	}

	void composeVolumePrimitiveLauncher(
		float* d_base, float* d_out,
		int3 volSize, int id,
		float4 param, float3 offset,
		float3 basisX, float3 basisY, float3 basisZ,
		int op) {

		if (!d_base || !d_out) return;

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z
		);

		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z)
		);

		composeVolumePrimitiveKernel << <gridSize, blockSize >> > (
			d_base, d_out,
			op, id, volSize,
			offset, param,
			basisX, basisY, basisZ
			);

		getLastCudaError("composeVolumePrimitiveKernel failed");
	}

	void composeVolumeFieldsLauncher(
		const float* d_base,
		const float* d_brush,
		float* d_out,
		int3 volSize,
		int op) {

		if (!d_base || !d_brush || !d_out) return;
		if (volSize.x <= 0 ||
			volSize.y <= 0 ||
			volSize.z <= 0) return;

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z
		);

		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z)
		);

		composeVolumeFieldsKernel << <gridSize, blockSize >> > (
			d_base, d_brush, d_out,
			volSize, op
			);

		getLastCudaError("composeVolumeFidlsKernel failed");

	}

	void transformVolumeFieldLauncher(
		const float* d_source,
		float* d_destination,
		int3 volSize,
		float3 scale,
		float3 offset,
		float3 basisX,
		float3 basisY,
		float3 basisZ) {

		if (!d_source || !d_destination) return;
		if (volSize.x <= 0 || volSize.y <= 0 || volSize.z <= 0) return;

		// Field transformation requires separate source and destination
		// buffers because each output voxel samples neighboring source
		// voxels.
		if (d_source == d_destination) {
			printf(
				"[transformVolumeFieldLauncher] "
				"Source and destination buffers must be different.\n"
			);

			return;
		}

		dim3 blockSize(
			VOLUME_KERNEL_BLOCK_X,
			VOLUME_KERNEL_BLOCK_Y,
			VOLUME_KERNEL_BLOCK_Z
		);

		dim3 gridSize(
			divUpInt(volSize.x, VOLUME_KERNEL_BLOCK_X),
			divUpInt(volSize.y, VOLUME_KERNEL_BLOCK_Y),
			divUpInt(volSize.z, VOLUME_KERNEL_BLOCK_Z)
		);

		transformVolumeFieldKernel << <gridSize, blockSize >> > (
			d_source, d_destination,
			volSize, scale, offset,
			basisX, basisY, basisZ
			);

		getLastCudaError(
			"transformVolumeFieldKernel failed"
		);
	}
	void classifyVolumeBoundaryLauncher(
		const float* d_volume, uchar* d_boundaryMask, uint* d_unsafeCount,
		uint* d_insideSampleCount,
		int3 volSize, float isoValue, float safetyBand) {

		if (!d_volume || !d_boundaryMask || !d_unsafeCount) return;
		if (volSize.x <= 0 || volSize.y <= 0 || volSize.z <= 0) return;


		const uint faceStride =
			getVolumeBoundaryFaceStride(volSize);

		const size_t maskBytes =
			getVolumeBoundaryMaskBytes(volSize);

		// This must test maskBytes == 0.
		//
		// The previous code tested merely "maskBytes", which caused
		// every valid nonzero allocation size to return immediately.
		if (faceStride == 0 || maskBytes == 0) return;

		const uint totalSlots =
			faceStride * static_cast<uint>(VOLUME_BOUNDARY_FACE_COUNT);

		cudaMemset(d_boundaryMask, 0, maskBytes);
		cudaMemset(d_unsafeCount, 0, sizeof(uint));
		if (d_insideSampleCount) {
			cudaMemset(d_insideSampleCount, 0, sizeof(uint));
		}

		const dim3 blockSize(VOLUME_BOUNDARY_BLOCK_SIZE, 1, 1);
		const dim3 gridSize(divUpInt(static_cast<int>(totalSlots),
			VOLUME_BOUNDARY_BLOCK_SIZE), 1, 1);

		classifyVolumeBoundaryKernel << <gridSize, blockSize >> > (
			d_volume, d_boundaryMask, d_unsafeCount,
			volSize, faceStride, isoValue, safetyBand);

		getLastCudaError(
			"classifyVolumeBoundaryKernel failed"
		);

		if (d_insideSampleCount) {
			const uint volumeSampleCount =
				static_cast<uint>(volSize.x) *
				static_cast<uint>(volSize.y) *
				static_cast<uint>(volSize.z);

			const dim3 volumeGridSize(
				divUpInt(
					static_cast<int>(volumeSampleCount),
					VOLUME_BOUNDARY_BLOCK_SIZE
				),
				1,
				1
			);

			countVolumeInsideSamplesKernel << <volumeGridSize, blockSize >> > (
				d_volume,
				d_insideSampleCount,
				volSize,
				isoValue
			);

			getLastCudaError(
				"countVolumeInsideSamplesKernel failed"
			);
		}
		// The kernel overwrites all valid and padding slots, but clearing
		// here also guarantees deterministic safe state if the layout changes.
	}
	///-----------------------------------------------------------------------------------------
	/// <MARCHING CUBES TABLE MANAGEMENT>
	///-----------------------------------------------------------------------------------------

	void allocateMarchingCubesTables(
		uint** d_edgeTable,
		uint** d_triTable,
		uint** d_numVertsTable) {

		cudaMalloc(
			reinterpret_cast<void**>(d_edgeTable),
			256 * sizeof(uint)
		);

		cudaMemcpy(
			*d_edgeTable,
			edgeTable,
			256 * sizeof(uint),
			cudaMemcpyHostToDevice
		);

		cudaMalloc(
			reinterpret_cast<void**>(d_triTable),
			256 * 16 * sizeof(uint)
		);

		cudaMemcpy(
			*d_triTable,
			triTable,
			256 * 16 * sizeof(uint),
			cudaMemcpyHostToDevice
		);

		cudaMalloc(
			reinterpret_cast<void**>(d_numVertsTable),
			256 * sizeof(uint)
		);

		cudaMemcpy(
			*d_numVertsTable,
			numVertsTable,
			256 * sizeof(uint),
			cudaMemcpyHostToDevice
		);
	}

	void freeMarchingCubesTables(
		uint* d_edgeTable,
		uint* d_triTable,
		uint* d_numVertsTable) {
		if (d_edgeTable) {
			cudaFree(d_edgeTable);
		}

		if (d_triTable) {
			cudaFree(d_triTable);
		}

		if (d_numVertsTable) {
			cudaFree(d_numVertsTable);
		}
	}

	///-----------------------------------------------------------------------------------------
	/// </MARCHING CUBES TABLE MANAGEMENT>
	///-----------------------------------------------------------------------------------------





	///-----------------------------------------------------------------------------------------
	/// <MARCHING CUBES KERNEL LAUNCHERS>
	///-----------------------------------------------------------------------------------------

	void setMarchingCubesParameters(MarchingCubesParams* hostParams) {
		cudaMemcpyToSymbol(
			mcParams,
			hostParams,
			sizeof(MarchingCubesParams)
		);
	}

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
		float isoValue) {
		mcClassifyVoxelKernel << <grid, threads >> > (
			voxelVerts,
			voxelOccupied,
			volume,
			numVertsTable,
			gridSize,
			gridSizeShift,
			gridSizeMask,
			numVoxels,
			voxelSize,
			isoValue
			);

		getLastCudaError("classifyVoxel failed");
	}

	void launchMCCompactVoxels(
		dim3 grid,
		dim3 threads,
		uint* compactedVoxelArray,
		uint* voxelOccupied,
		uint* voxelOccupiedScan,
		uint numVoxels) {

		mcCompactVoxelsKernel << <grid, threads >> > (
			compactedVoxelArray,
			voxelOccupied,
			voxelOccupiedScan,
			numVoxels
			);

		getLastCudaError("compactVoxels failed");
	}

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
		uint maxVerts) {
		// The original MC sample intentionally launches with NTHREADS here
		// because the shared-memory triangle kernel assumes NTHREADS slots.
		// So we keep that behavior, but the caller should pass threads = dim3(NTHREADS).
		(void)threads;

		mcGenerateTrianglesKernel << <grid, NTHREADS >> > (
			pos,
			norm,
			compactedVoxelArray,
			numVertsScanned,
			volume,
			numVertsTable,
			triTable,
			gridSize,
			gridSizeShift,
			gridSizeMask,
			voxelSize,
			isoValue,
			activeVoxels,
			maxVerts
			);

		getLastCudaError("generateTriangles failed");
	}

	void mcThrustScanWrapper(
		uint* output,
		uint* input,
		uint numElements) {

		thrust::device_ptr<uint> d_input(input);
		thrust::device_ptr<uint> d_output(output);

		thrust::exclusive_scan(
			d_input,
			d_input + numElements,
			d_output
		);
	}

	///-----------------------------------------------------------------------------------------
	/// </MARCHING CUBES KERNEL LAUNCHERS>
	///-----------------------------------------------------------------------------------------

}
