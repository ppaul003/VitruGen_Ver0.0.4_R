#ifndef _KERNEL_IMPL_CUH_
#define _KERNEL_IMPL_CUH_

#include <stdio.h>
#include <math.h>
#include "vector_types.h"
#include <helper_math.h>
//#include <math_constants.h>

#include "params_kernel.cuh"

#define EPS 0.01f
#define NUMSTEPS 20

#if USE_TEX
texture<float4, 1, cudaReadModeElementType> oldPosTex;
texture<float4, 1, cudaReadModeElementType> oldVelTex;

texture<uint, 1, cudaReadModeElementType> gridParticleHashTex;
texture<uint, 1, cudaReadModeElementType> cellStartTex;
texture<uint, 1, cudaReadModeElementType> cellEndTex;
#endif

#ifndef VOLUME_KERNEL_BLOCK_X
#define VOLUME_KERNEL_BLOCK_X 8
#endif

#ifndef VOLUME_KERNEL_BLOCK_Y
#define VOLUME_KERNEL_BLOCK_Y 8
#endif

#ifndef VOLUME_KERNEL_BLOCK_Z
#define VOLUME_KERNEL_BLOCK_Z 8
#endif

#ifndef VOLUME_BOUNDARY_BLOCK_SIZE
#define VOLUME_BOUNDARY_BLOCK_SIZE 256
#endif

#ifndef MC_WORKSPACE_HALF_EXTENT
#define MC_WORKSPACE_HALF_EXTENT 64.0f
#endif

#ifndef MC_EPSILON
#define MC_EPSILON 1.0e-6f
#endif

#define MC_GRID_STEPS 96
#define MC_GRID_MAJOR_EVERY 8
#define MC_GRID_THICKNESS 0.18f

typedef struct {
	float3 o, d; // origin and direction
} Ray;

__constant__ SimParams cSimParams;

__constant__ MarchingCubesParams mcParams;

struct integrate_functor {

	float deltaTime;

	__host__ __device__ integrate_functor(float delta_time) : deltaTime(delta_time) {}

	template <typename Tuple>
	__device__ void operator()(Tuple t) {

		volatile float4 posData = thrust::get<0>(t);
		volatile float4 velData = thrust::get<1>(t);
		volatile float4 accData = thrust::get<2>(t);

		float3 pos = make_float3(posData.x, posData.y, posData.z);
		float3 vel = make_float3(velData.x, velData.y, velData.z);
		float3 acc = make_float3(accData.x, accData.y, accData.z);

		vel += acc * deltaTime;
		vel += cSimParams.gravity * deltaTime;
		vel *= cSimParams.globalDamping;

		// new position = old position + velocity * deltaTime
		pos += vel * deltaTime;

		// set this to zero to disable collisions with cube sides
#if 1
		if (pos.x > cSimParams.boundary - velData.w) {
			pos.x = cSimParams.boundary - velData.w;
			vel.x *= cSimParams.boundaryDamping;
		}
		if (pos.x < -cSimParams.boundary + velData.w) {
			pos.x = -cSimParams.boundary + velData.w;
			vel.x *= cSimParams.boundaryDamping;
		}

		if (pos.y > cSimParams.boundary - velData.w) {
			pos.y = cSimParams.boundary - velData.w;
			vel.y *= cSimParams.boundaryDamping;
		}

		if (pos.z > cSimParams.boundary - velData.w) {
			pos.z = cSimParams.boundary - velData.w;
			vel.z *= cSimParams.boundaryDamping;
		}
		if (pos.z < -cSimParams.boundary + velData.w) {
			pos.z = -cSimParams.boundary + velData.w;
			vel.z *= cSimParams.boundaryDamping;
		}
		if (pos.y < -cSimParams.boundary + velData.w) {
			pos.y = -cSimParams.boundary + velData.w;
			vel.y *= cSimParams.boundaryDamping;
		}

#endif

		// store new position and velocity
		thrust::get<0>(t) = make_float4(pos, posData.w);
		thrust::get<1>(t) = make_float4(vel, velData.w);
	}
};

///-----------------------------------------------------------------------------------------
/// <PARTICLE SYSTEM HELPERS>
///-----------------------------------------------------------------------------------------
// calculate position in uniform grid
__device__
int3 calcGridPos(float3 p) {
	int3 gridPos;
	gridPos.x = floor((p.x - cSimParams.worldOrigin.x) / cSimParams.cellSize.x);
	gridPos.y = floor((p.y - cSimParams.worldOrigin.y) / cSimParams.cellSize.y);
	gridPos.z = floor((p.z - cSimParams.worldOrigin.z) / cSimParams.cellSize.z);
	return gridPos;
}

// calculate address in grid from position (clamping to edges)
__device__
uint calcGridHash(int3 gridPos) {
	gridPos.x = gridPos.x & (cSimParams.gridSize.x - 1); // wrap grid, assumes size is power of 2
	gridPos.y = gridPos.y & (cSimParams.gridSize.y - 1);
	gridPos.z = gridPos.z & (cSimParams.gridSize.z - 1);
	return ((gridPos.z * cSimParams.gridSize.y) * cSimParams.gridSize.x) + (gridPos.y * cSimParams.gridSize.x) + gridPos.x;
}

// calculate grid hash value for each particle
__device__
float3 bodyBodyInteractions(
	float4 bi,
	float4 bj,
	float3 ai) {

	float3 posi = make_float3(bi);
	float3 posj = make_float3(bj);

	float3 r = posj - posi;

	float distSqr = (r.x * r.x) + (r.y * r.y) + (r.z * r.z) + EPS2;

	float distSixth = distSqr * distSqr * distSqr;
	float invDistSqr = 1.0f / sqrtf(distSixth);

	float gm_r3 = G * bj.w * invDistSqr;

	ai.x += gm_r3 * r.x;
	ai.y += gm_r3 * r.y;
	ai.z += gm_r3 * r.z;

	return ai;
}
__device__ float3 tile_calculation(float4 myPosition, float3 acc, uint tile, uint numParticles) {
	extern __shared__ float4 shPosition[];

	for (uint j = 0; j < blockDim.x; j++) {
		uint other = tile * blockDim.x + j;
		if (other < numParticles) {
			acc = bodyBodyInteractions(myPosition, shPosition[j], acc);
		}
	}

	return acc;
}
__device__
float3 collideSpheres(
	float4 posA,
	float4 posB,
	float4 velA,
	float4 velB,
	float attraction) {

	const float3 pos_A = make_float3(posA);
	const float3 pos_B = make_float3(posB);
	const float3 vel_A = make_float3(velA);
	const float3 vel_B = make_float3(velB);

	float3 relPos = pos_B - pos_A;
	float dist = length(relPos);

	float collideDist = velA.w + velB.w;

	float3 force = make_float3(0.0f);

	if (dist < collideDist) {
		float3 norm = relPos / dist;
		float3 relVel = vel_B - vel_A;
		float3 tanVel = relVel - (dot(relVel, norm) * norm);

		// spring force
		force = -cSimParams.spring * (collideDist - dist) * norm;
		// dashpot (damping) force
		force += cSimParams.damping * relVel;
		// tangential shear force
		force += cSimParams.shear * tanVel;
		// attraction
		force += cSimParams.attraction * relPos;
	}

	return force;
}
__device__
float3 collideCell(
	int3 gridPos,
	uint index,
	float4 pos,
	float4 vel,
	float4* oldPos,
	float4* oldVel,
	uint* cellStart,
	uint* cellEnd) {

	uint gridHash = calcGridHash(gridPos);
	uint startIndex = FETCH(cellStart, gridHash);

	float3 force = make_float3(0.0f);
	if (startIndex != 0xffffffff) { // cell is not empty

		// iterate over particles in this cell
		uint endIndex = FETCH(cellEnd, gridHash);

		for (int j = startIndex; j < endIndex; j++) {
			if (j != index) { // check not colliding with self

				float4 pos2 = FETCH(oldPos, j);
				float4 vel2 = FETCH(oldVel, j);

				force += collideSpheres(pos, pos2, vel, vel2, cSimParams.attraction);
			}
		}
	}

	return force;
}
///-----------------------------------------------------------------------------------------
/// </PARTICLE SYSTEM HELPERS>
///-----------------------------------------------------------------------------------------





///-----------------------------------------------------------------------------------------
/// <VOLUME FIELD HELPERS>
///-----------------------------------------------------------------------------------------

__host__ uint divUp(int a, int b) {
	return (a + b - 1) / b;
}

__device__
unsigned char clip(int n) {
	return n > 255 ? 255
		: (n < 0 ? 0 : n);
}

__device__
int clipWithBounds(int n, int n_min, int n_max) {
	return n > n_max ? n_max
		: (n < n_min ? n_min : n);
}
__device__
float3 xRotate(float3 pos, float phi) {
	const float c = cosf(phi), s = sinf(phi);

	return make_float3(
		pos.x,
		c * pos.y - s * pos.z,
		s * pos.y + c * pos.z
	);
}
__device__
float3 yRotate(float3 pos, float theta) {
	const float c = cosf(theta), s = sinf(theta);

	return make_float3(
		c * pos.x + s * pos.z,
		pos.y,
		-s * pos.x + c * pos.z
	);
}
__device__
float sdfBoxVoxel(
	float x, float y, float z,
	float bx, float by, float bz) {

	bx = fmaxf(bx, 0.001f);
	by = fmaxf(by, 0.001f);
	bz = fmaxf(bz, 0.001f);

	float qx = fabsf(x) - bx;
	float qy = fabsf(y) - by;
	float qz = fabsf(z) - bz;

	const float outsideX = fmaxf(qx, 0.0f);
	const float outsideY = fmaxf(qy, 0.0f);
	const float outsideZ = fmaxf(qz, 0.0f);

	const float outside =
		sqrtf(
			outsideX * outsideX +
			outsideY * outsideY +
			outsideZ * outsideZ
		);

	const float inside =
		fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);

	return outside + inside;
}
__device__
float sdfCylinderZ(
	float x, float y, float z,
	float radius, float halfHeight) {

	radius = fmaxf(radius, 0.001f);
	halfHeight = fmaxf(halfHeight, 0.001f);

	const float radial =
		sqrtf(x * x + y * y) - radius;

	const float cap =
		fabsf(z) - halfHeight;

	const float outsideR = fmaxf(radial, 0.0f);
	const float outsideZ = fmaxf(cap, 0.0f);

	const float outside =
		sqrtf(outsideR * outsideR + outsideZ * outsideZ);

	const float inside =
		fminf(fmaxf(radial, cap), 0.0f);

	return outside + inside;
}
__device__
float sdfCapsuleZ(
	float x, float y, float z,
	float radius, float halfSegmentLength) {

	radius = fmaxf(radius, 0.001f);
	halfSegmentLength = fmaxf(halfSegmentLength, 0.001f);

	const float zClamped =
		fminf(
			fmaxf(z, -halfSegmentLength),
			halfSegmentLength
		);

	const float dz = z - zClamped;

	return sqrtf(x * x + y * y + dz * dz) - radius;
}

__device__
float sdfWedgeY(
	float x, float y, float z,
	float bx, float by, float bz) {

	bx = fmaxf(bx, 0.001f);
	by = fmaxf(by, 0.001f);
	bz = fmaxf(bz, 0.001f);

	// Base bounding box.
	const float boxD = sdfBoxVoxel(x, y, z, bx, by, bz);

	// Sloped top plane:
	// at x = -bx, top is z = -bz
	// at x = +bx, top is z = +bz
	const float slope = bz / bx;

	const float planeD =
		(z - x * slope) /
		sqrtf(1.0f + slope * slope);

	// Intersection of box and half-space.
	// Inside means inside box AND below sloped plane.
	return fmaxf(boxD, planeD);
}
__device__
float sdfRectFrustumZApprox(
	float x, float y, float z,
	float bottomHalfX, float bottomHalfY,
	float halfHeight, float topScale) {

	bottomHalfX = fmaxf(bottomHalfX, 0.001f);
	bottomHalfY = fmaxf(bottomHalfY, 0.001f);
	halfHeight = fmaxf(halfHeight, 0.001f);

	topScale = fminf(fmaxf(topScale, 0.05f), 1.0f);

	// t = 0 at bottom, t = 1 at top.
	float t =
		(z / halfHeight) * 0.5f + 0.5f;

	t = fminf(fmaxf(t, 0.0f), 1.0f);

	const float halfX =
		bottomHalfX * (1.0f + (topScale - 1.0f) * t);

	const float halfY =
		bottomHalfY * (1.0f + (topScale - 1.0f) * t);

	float qx = fabsf(x) - halfX;
	float qy = fabsf(y) - halfY;
	float qz = fabsf(z) - halfHeight;

	const float outsideX = fmaxf(qx, 0.0f);
	const float outsideY = fmaxf(qy, 0.0f);
	const float outsideZ = fmaxf(qz, 0.0f);

	const float outside =
		sqrtf(
			outsideX * outsideX +
			outsideY * outsideY +
			outsideZ * outsideZ
		);

	const float inside =
		fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);

	// This is a sign-correct approximation, not a perfect Euclidean SDF.
	return outside + inside;
}

__device__
float sdfConeZApprox(
	float x, float y, float z,
	float bottomRadius, float halfHeight) {
	bottomRadius = fmaxf(bottomRadius, 0.001f);
	halfHeight = fmaxf(halfHeight, 0.001f);
	float t = (z / halfHeight) * 0.5f + 0.5f;
	t = fminf(fmaxf(t, 0.0f), 1.0f);
	const float radiusAtZ = bottomRadius * (1.0f - t);
	const float radial = sqrtf(x * x + y * y) - radiusAtZ;
	const float cap = fabsf(z) - halfHeight;
	const float outsideRadial = fmaxf(radial, 0.0f);
	const float outsideCap = fmaxf(cap, 0.0f);
	const float outside = sqrtf(
		outsideRadial * outsideRadial + outsideCap * outsideCap);
	const float inside = fminf(fmaxf(radial, cap), 0.0f);
	return outside + inside;
}

__device__
float sdfDeltaWingApprox(
	float x, float y, float z,
	float halfWidth, float halfLength, float halfThickness) {
	halfWidth = fmaxf(halfWidth, 0.001f);
	halfLength = fmaxf(halfLength, 0.001f);
	halfThickness = fmaxf(halfThickness, 0.001f);

	// Reference orientation: broad trailing edge at local -Y, point at +Y,
	// span on local X, and extrusion thickness on local Z.
	const float normalizedY =
		fminf(fmaxf((y + halfLength) / (2.0f * halfLength), 0.0f), 1.0f);
	const float widthAtY = halfWidth * (1.0f - normalizedY);
	const float side = fabsf(x) - widthAtY;
	const float bottom = -halfLength - y;
	const float top = y - halfLength;
	const float thickness = fabsf(z) - halfThickness;
	const float q = fmaxf(fmaxf(side, bottom), fmaxf(top, thickness));
	if (q <= 0.0f) return q;
	const float ox = fmaxf(side, 0.0f);
	const float oy = fmaxf(fmaxf(bottom, top), 0.0f);
	const float oz = fmaxf(thickness, 0.0f);
	return sqrtf(ox * ox + oy * oy + oz * oz);
}

__device__
float funcSDFLocal(
	float dx, float dy, float dz,
	float id, float4 param) {
	// id 0: ellipsoid / sphere
	if (id == 0) {
		const float rx = fmaxf(param.x, 0.001f);
		const float ry = fmaxf(param.y, 0.001f);
		const float rz = fmaxf(param.z, 0.001f);

		const float qx = dx / rx;
		const float qy = dy / ry;
		const float qz = dz / rz;

		const float q =
			sqrtf(qx * qx + qy * qy + qz * qz) - 1.0f;

		const float minR = fminf(rx, fminf(ry, rz));

		return q * minR;
	}

	// id 1: torus around Z axis
	if (id == 1) {
		const float majorR = fmaxf(param.x, 0.001f);
		const float minorR = fmaxf(param.y, 0.001f);

		const float ring =
			sqrtf(dx * dx + dy * dy) - majorR;

		return sqrtf(ring * ring + dz * dz) - minorR;
	}

	// id 2: block
	if (id == 2) {
		return sdfBoxVoxel(
			dx, dy, dz,
			param.x,
			param.y,
			param.z
		);
	}

	// id 3: cylinder along Z
	if (id == 3) {
		return sdfCylinderZ(
			dx, dy, dz,
			param.x,
			param.z
		);
	}

	// id 4: capsule along Z
	if (id == 4) {
		return sdfCapsuleZ(
			dx, dy, dz,
			param.x,
			param.z
		);
	}

	// id 5: wedge / ramp prism
	if (id == 5) {
		return sdfWedgeY(
			dx, dy, dz,
			param.x,
			param.y,
			param.z
		);
	}

	// id 6: rectangular frustum / truncated pyramid
	if (id == 6) {
		return sdfRectFrustumZApprox(
			dx, dy, dz,
			param.x,
			param.y,
			param.z,
			param.w
		);
	}

	// id 7: circular cone along local Z
	if (id == 7) {
		return sdfConeZApprox(dx, dy, dz, param.x, param.z);
	}

	// id 8: extruded triangular delta wing
	if (id == 8) {
		return sdfDeltaWingApprox(dx, dy, dz, param.x, param.y, param.z);
	}

	// fallback: block
	return sdfBoxVoxel(
		dx, dy, dz,
		param.x,
		param.y,
		param.z
	);
}

__device__
float3 worldPointToObjectLocalBasis(
	float3 worldPoint,
	float3 basisX,
	float3 basisY,
	float3 basisZ) {

	// Basis vectors are orthonormal and stored as object-local axes
	// expressed in world coordinates.
	//
	// Inverse basis transform = transpose(basis).
	return make_float3(
		worldPoint.x * basisX.x +
		worldPoint.y * basisX.y +
		worldPoint.z * basisX.z,

		worldPoint.x * basisY.x +
		worldPoint.y * basisY.y +
		worldPoint.z * basisY.z,

		worldPoint.x * basisZ.x +
		worldPoint.y * basisZ.y +
		worldPoint.z * basisZ.z
	);
}
__device__
float funcSDFOffset(
	int c, int r, int s,
	int id, int3 volSize,
	float4 param,
	float3 basisX,
	float3 basisY,
	float3 basisZ,
	float3 offset) {

	const float cx = 0.5f * static_cast<float>(volSize.x) + offset.x;
	const float cy = 0.5f * static_cast<float>(volSize.y) + offset.y;
	const float cz = 0.5f * static_cast<float>(volSize.z) + offset.z;

	const float3 worldPoint = make_float3(
		static_cast<float>(c) - cx,
		static_cast<float>(r) - cy,
		static_cast<float>(s) - cz
	);

	const float3 localPoint =
		worldPointToObjectLocalBasis(
			worldPoint,
			basisX,
			basisY,
			basisZ
		);

	return funcSDFLocal(
		localPoint.x,
		localPoint.y,
		localPoint.z,
		id,
		param
	);
}
__device__ float funcSDF(
	int c, int r, int s,
	int id, int3 volSize,
	float4 param,
	float3 basisX,
	float3 basisY,
	float3 basisZ) {

	const float3 worldPoint = make_float3(
		static_cast<float>(c) - 0.5f * static_cast<float>(volSize.x),
		static_cast<float>(r) - 0.5f * static_cast<float>(volSize.y),
		static_cast<float>(s) - 0.5f * static_cast<float>(volSize.z)
	);

	const float3 localPoint =
		worldPointToObjectLocalBasis(
			worldPoint,
			basisX,
			basisY,
			basisZ
		);

	return funcSDFLocal(
		localPoint.x,
		localPoint.y,
		localPoint.z,
		id,
		param
	);
}
__device__
float3 scrIdxToPos(
	int c, int r,
	int w, int h,
	int zs) {

	return make_float3(
		c - w / 2,
		r - h / 2,
		zs
	);
}
__device__
float3 paramRay(Ray r, float t) {
	return r.o + t * (r.d);
}
__device__
float planeSDF(float3 pos, float3 norm, float d) {
	return dot(pos, normalize(norm)) - d;
}
__device__
bool rayPlaneIntersect(
	Ray myRay,
	float3 n,
	float dist,
	float* t) {

	const float f0 = planeSDF(paramRay(myRay, 0.f), n, dist);
	const float f1 = planeSDF(paramRay(myRay, 1.f), n, dist);

	bool result = (f0 * f1 < 0);
	if (result)
		*t = (0.f - f0) / (f1 - f0);

	return result;
}

// Intersect ray with a box from volumeRender SDK sample.
__device__
bool intersectBox(
	Ray r,
	float3 boxmin,
	float3 boxmax,
	float* tnear,
	float* tfar) {

	// Compute intersection of ray with all six bbox planes.
	const float3 invR = make_float3(1.0f) / r.d;
	const float3 tbot = invR * (boxmin - r.o);
	const float3 ttop = invR * (boxmax - r.o);

	// Re-order intersections to find smallest and largest on each axis.
	const float3 tmin = fminf(ttop, tbot);
	const float3 tmax = fmaxf(ttop, tbot);

	// Find the largest tmin and the smallest tmax.
	*tnear = fmaxf(
		fmaxf(tmin.x, tmin.y),
		fmaxf(tmin.x, tmin.z)
	);

	*tfar = fminf(
		fminf(tmax.x, tmax.y),
		fminf(tmax.x, tmax.z)
	);

	return *tfar > *tnear;
}
__device__
int3 posToVolIndex(float3 pos, int3 volSize) {
	return make_int3(
		pos.x + volSize.x / 2,
		pos.y + volSize.y / 2,
		pos.z + volSize.z / 2
	);
}
__device__
int flatten(int3 index, int3 volSize) {
	return index.x +
		index.y * volSize.x +
		index.z * volSize.x * volSize.y;
}
__device__
float density(
	float* d_vol,
	int3 volSize,
	float3 pos) {
	int3 index = posToVolIndex(pos, volSize);

	int i = index.x;
	int j = index.y;
	int k = index.z;

	const int w = volSize.x;
	const int h = volSize.y;
	const int d = volSize.z;

	const float3 rem = fracf(pos);
	index = make_int3(
		clipWithBounds(i, 0, w - 2),
		clipWithBounds(j, 0, h - 2),
		clipWithBounds(k, 0, d - 2)
	);

	// directed increments for computing the gradient
	const int3 dx = { 1, 0, 0 };
	const int3 dy = { 0, 1, 0 };
	const int3 dz = { 0, 0, 1 };

	// values sampled at surrounding grid points
	const float dens000 = d_vol[flatten(index, volSize)];
	const float dens100 = d_vol[flatten(index + dx, volSize)];
	const float dens010 = d_vol[flatten(index + dy, volSize)];
	const float dens001 = d_vol[flatten(index + dz, volSize)];
	const float dens110 = d_vol[flatten(index + dx + dy, volSize)];
	const float dens101 = d_vol[flatten(index + dx + dz, volSize)];
	const float dens011 = d_vol[flatten(index + dy + dz, volSize)];
	const float dens111 = d_vol[flatten(index + dx + dy + dz, volSize)];

	// trilinear interpolation
	return (1 - rem.x) * (1 - rem.y) * (1 - rem.z) * dens000 +
		(rem.x) * (1 - rem.y) * (1 - rem.z) * dens100 +
		(1 - rem.x) * (rem.y) * (1 - rem.z) * dens010 +
		(1 - rem.x) * (1 - rem.y) * (rem.z) * dens001 +
		(rem.x) * (rem.y) * (1 - rem.z) * dens110 +
		(rem.x) * (1 - rem.y) * (rem.z) * dens101 +
		(1 - rem.x) * (rem.y) * (rem.z) * dens011 +
		(rem.x) * (rem.y) * (rem.z) * dens111;
}

__device__ __forceinline__
float sanitizeVolumeFieldSample(float value) {

	// A transformed field must remain usable as a distance field.
	// Huge clear/sentinel values make the sphere tracer jump over
	// the entire volume.
	if (value != value ||
		fabsf(value) >= 5.0e5f) {

		return 1.0f;
	}

	return value;
}

__device__
float sampleVolumeFieldTrilinear(
	const float* source,
	int3 volSize,
	float3 centeredPosition) {

	// Convert centered volume coordinates into voxel coordinates.
	const float rawGX =
		centeredPosition.x +
		0.5f * static_cast<float>(volSize.x);

	const float rawGY =
		centeredPosition.y +
		0.5f * static_cast<float>(volSize.y);

	const float rawGZ =
		centeredPosition.z +
		0.5f * static_cast<float>(volSize.z);

	// Clamp the sampling position to the source volume boundary.
	//
	// We do not immediately return 1.0e6f when the transformed
	// coordinate falls outside the source field. Doing that breaks
	// sphere tracing whenever BASE is scaled below 1.0.
	const float gx = fminf(
		fmaxf(rawGX, 0.0f),
		static_cast<float>(volSize.x - 1)
	);

	const float gy = fminf(
		fmaxf(rawGY, 0.0f),
		static_cast<float>(volSize.y - 1)
	);

	const float gz = fminf(
		fmaxf(rawGZ, 0.0f),
		static_cast<float>(volSize.z - 1)
	);

	// Distance traveled beyond the available source field.
	//
	// Adding this to the boundary SDF extends the field outward
	// with a finite, distance-like value instead of an enormous
	// sentinel.
	const float outsideDX = rawGX - gx;
	const float outsideDY = rawGY - gy;
	const float outsideDZ = rawGZ - gz;

	const float outsideDistance =
		sqrtf(
			outsideDX * outsideDX +
			outsideDY * outsideDY +
			outsideDZ * outsideDZ
		);

	const int x0 =
		static_cast<int>(floorf(gx));

	const int y0 =
		static_cast<int>(floorf(gy));

	const int z0 =
		static_cast<int>(floorf(gz));

	const int x1 =
		min(x0 + 1, volSize.x - 1);

	const int y1 =
		min(y0 + 1, volSize.y - 1);

	const int z1 =
		min(z0 + 1, volSize.z - 1);

	const float tx =
		gx - static_cast<float>(x0);

	const float ty =
		gy - static_cast<float>(y0);

	const float tz =
		gz - static_cast<float>(z0);

	const int wh =
		volSize.x * volSize.y;

	const float d000 =
		sanitizeVolumeFieldSample(
			source[x0 + y0 * volSize.x + z0 * wh]
		);

	const float d100 =
		sanitizeVolumeFieldSample(
			source[x1 + y0 * volSize.x + z0 * wh]
		);

	const float d010 =
		sanitizeVolumeFieldSample(
			source[x0 + y1 * volSize.x + z0 * wh]
		);

	const float d110 =
		sanitizeVolumeFieldSample(
			source[x1 + y1 * volSize.x + z0 * wh]
		);

	const float d001 =
		sanitizeVolumeFieldSample(
			source[x0 + y0 * volSize.x + z1 * wh]
		);

	const float d101 =
		sanitizeVolumeFieldSample(
			source[x1 + y0 * volSize.x + z1 * wh]
		);

	const float d011 =
		sanitizeVolumeFieldSample(
			source[x0 + y1 * volSize.x + z1 * wh]
		);

	const float d111 =
		sanitizeVolumeFieldSample(
			source[x1 + y1 * volSize.x + z1 * wh]
		);

	const float d00 =
		d000 + tx * (d100 - d000);

	const float d10 =
		d010 + tx * (d110 - d010);

	const float d01 =
		d001 + tx * (d101 - d001);

	const float d11 =
		d011 + tx * (d111 - d011);

	const float d0 =
		d00 + ty * (d10 - d00);

	const float d1 =
		d01 + ty * (d11 - d01);

	const float interpolated =
		d0 + tz * (d1 - d0);

	// Finite continuation beyond the source-volume boundary.
	return interpolated + outsideDistance;
}

__device__
float3 makePerspectiveRayDir(
	int c, int r,
	int w, int h,
	float fovYDegrees) {

	float aspect = (float)w / (float)h;
	float fovYRadians = fovYDegrees * 0.01745329251994329577f;
	float tanHalfFovY = tanf(fovYRadians * 0.5f);

	// normalized device coordinates: x,y in [-1, +1]
	float ndcX = (2.0f * ((float)c + 0.5f) / (float)w) - 1.0f;
	float ndcY = 1.0f - (2.0f * ((float)r + 0.5f) / (float)h);

	float px = ndcX * aspect * tanHalfFovY;
	float py = ndcY * tanHalfFovY;

	return normalize(make_float3(px, py, -1.0f));
}

__device__
uchar4 sliceShader(
	float* d_vol, int3 volSize,
	Ray boxRay,
	float gain, float dist,
	float3 norm) {

	float t;
	uchar4 shade = make_uchar4(96, 0, 192, 0); // background value

	if (rayPlaneIntersect(boxRay, norm, dist, &t)) {
		float sliceDens = density(
			d_vol,
			volSize,
			paramRay(boxRay, t)
		);

		shade = make_uchar4(
			48,
			clip(-10.f * (1.0f + gain) * sliceDens),
			96, 255
		);
	}
	return shade;
}
__device__
uchar4 volumeRenderShader(
	float* d_vol,
	int3 volSize,
	Ray boxRay,
	float threshold,
	int numSteps,
	float tintR,
	float tintG,
	float tintB) {

	uchar4 shade = make_uchar4(96, 0, 192, 0); // background value

	const float dt = 1.f / numSteps;
	const float len = length(boxRay.d) / numSteps;

	float accum = 0.f;
	float3 pos = boxRay.o;
	float val = density(d_vol, volSize, pos);

	for (float t = 0.f; t < 1.f; t += dt) {

		if (val - threshold < 0.f)
			accum += (fabsf(val - threshold)) * len;

		pos = paramRay(boxRay, t);
		val = density(d_vol, volSize, pos);
	}

	const int a = clip(accum);
	if (a > 0) {
		shade.x = clip(static_cast<int>(a * tintR));
		shade.y = clip(static_cast<int>(a * tintG));
		shade.z = clip(static_cast<int>(a * tintB));
		shade.w = 255;
	}

	return shade;
}

__device__
uchar4 rayCastShader(
	float* d_vol,
	int3 volSize,
	Ray boxRay,
	float dist,
	float tintR,
	float tintG,
	float tintB) {

	uchar4 shade = make_uchar4(96, 0, 192, 0);

	float3 pos = boxRay.o;
	float len = length(boxRay.d);

	float t = 0.0f;
	float f = density(d_vol, volSize, pos);

	while (f > dist + EPS && t < 1.0f) {

		f = density(d_vol, volSize, pos);
		t += (f - dist) / len;
		pos = paramRay(boxRay, t);
		f = density(d_vol, volSize, pos);
	}

	if (t < 1.f) {
		const float3 ux = make_float3(1, 0, 0);
		const float3 uy = make_float3(0, 1, 0);
		const float3 uz = make_float3(0, 0, 1);

		float3 grad = make_float3(
			(density(d_vol, volSize, pos + EPS * ux) -
				density(d_vol, volSize, pos)) / EPS,

			(density(d_vol, volSize, pos + EPS * uy) -
				density(d_vol, volSize, pos)) / EPS,

			(density(d_vol, volSize, pos + EPS * uz) -
				density(d_vol, volSize, pos)) / EPS
		);

		float intensity =
			-dot(normalize(boxRay.d), normalize(grad));

		//shade = make_uchar4(255 * intensity, 0, 0, 255);

		float gradLen = length(grad);

		if (gradLen > 1.0e-6f) {
			float intensity =
				-dot(normalize(boxRay.d), grad / gradLen);

			intensity = fminf(fmaxf(intensity, 0.0f), 1.0f);

			const float lit = 255.0f * intensity;

			shade = make_uchar4(
				clip(static_cast<int>(lit * tintR)),
				clip(static_cast<int>(lit * tintG)),
				clip(static_cast<int>(lit * tintB)),
				255
			);
		}
	}
	return shade;
}

__device__
uchar4 rayCastShaderForMC(
	float* d_vol,
	int3 volSize,
	Ray boxRay,
	float dist) {

	uchar4 shade = make_uchar4(96, 0, 192, 0);

	float3 pos = boxRay.o;
	float len = length(boxRay.d);

	float t = 0.0f;
	float f = density(d_vol, volSize, pos);

	while (f > dist + EPS && t < 1.0f) {

		f = density(d_vol, volSize, pos);
		t += (f - dist) / len;
		pos = paramRay(boxRay, t);
		f = density(d_vol, volSize, pos);
	}

	if (t < 1.f) {
		const float3 ux = make_float3(1, 0, 0);
		const float3 uy = make_float3(0, 1, 0);
		const float3 uz = make_float3(0, 0, 1);

		float3 grad = make_float3(
			(density(d_vol, volSize, pos + EPS * ux) -
				density(d_vol, volSize, pos)) / EPS,

			(density(d_vol, volSize, pos + EPS * uy) -
				density(d_vol, volSize, pos)) / EPS,

			(density(d_vol, volSize, pos + EPS * uz) -
				density(d_vol, volSize, pos)) / EPS
		);

		float intensity =
			-dot(normalize(boxRay.d), normalize(grad));

		//shade = make_uchar4(255 * intensity, 0, 0, 255);

		float gradLen = length(grad);

		if (gradLen > 1.0e-6f) {
			float intensity =
				-dot(normalize(boxRay.d), grad / gradLen);

			intensity = fminf(fmaxf(intensity, 0.0f), 1.0f);

			shade = make_uchar4(
				clip(static_cast<int>(255.0f * intensity)),
				0, 0, 255
			);
		}
	}
	return shade;
}

///-----------------------------------------------------------------------------------------
/// </VOLUME FIELD HELPERS>
///-----------------------------------------------------------------------------------------



///-----------------------------------------------------------------------------------------
/// <PARTICLE SYSTEM KERNEL>
///-----------------------------------------------------------------------------------------
__global__
void calcHashD(
	uint* gridParticleHash,
	uint* gridParticleIndex,
	float4* pos,
	uint numParticles) {

	const uint index = blockIdx.x * blockDim.x + threadIdx.x;

	if (index >= numParticles) return;
	volatile float4 p = pos[index];

	// get addres in grid
	int3 gridPos = calcGridPos(make_float3(p.x, p.y, p.z));
	uint hash = calcGridHash(gridPos);

	// store grid hash and particle index
	gridParticleHash[index] = hash;
	gridParticleIndex[index] = index;
}

__global__
void reorderDataAndFindCellStartD(
	uint* cellStart,
	uint* cellEnd,
	float4* sortedPos,
	float4* sortedVel,
	uint* gridParticleHash,
	uint* gridParticleIndex,
	float4* oldPos,
	float4* oldVel,
	uint numParticles) {

	extern __shared__ uint sharedHash[]; // blockSize + 1 elements
	const uint index = blockIdx.x * blockDim.x + threadIdx.x;

	uint hash;
	// handle case when no. of particles not multiple of block size
	if (index < numParticles) {
		hash = gridParticleHash[index];
		// Load hash data into shared memory so that we can look
		// at neighboring particle's hash value without loading
		// two hash values per thread
		sharedHash[threadIdx.x + 1] = hash;

		if (index > 0 && threadIdx.x == 0) {
			// first thread in block must load neighbor particle hash
			sharedHash[0] = gridParticleHash[index - 1];
		}
	}

	__syncthreads();

	if (index < numParticles) {
		// If this particle has a different cell index to the previous
		// particle then it must be the first particle in the cell,
		// so store the index of this particle in the cell.
		// As it isn't the first particle, it must also be the cell end of
		// the previous particle's cell
		if (index == 0 || hash != sharedHash[threadIdx.x]) {
			cellStart[hash] = index;
			if (index > 0)
				cellEnd[sharedHash[threadIdx.x]] = index;
		}
		if (index == numParticles - 1) {
			cellEnd[hash] = index + 1;
		}

		// Now use the sorted index to reorder the pos and vel data
		uint sortedIndex = gridParticleIndex[index];
		float4 pos = FETCH(oldPos, sortedIndex); // macro does either global read or texture fetch
		float4 vel = FETCH(oldVel, sortedIndex);

		sortedPos[index] = pos;
		sortedVel[index] = vel;
	}
}

__global__
void collideD(
	float4* newVel,
	float4* oldPos,
	float4* oldVel,
	uint* gridParticleIndex,
	uint* cellStart,
	uint* cellEnd,
	uint numParticles) {

	const uint index = blockIdx.x * blockDim.x + threadIdx.x;
	if (index >= numParticles) return;

	// read particle data from sorted arrays
	float4 pos = FETCH(oldPos, index);
	float4 vel = FETCH(oldVel, index);

	float3 v_new = make_float3(vel.x, vel.y, vel.z);

	// get address in grid
	int3 gridPos = calcGridPos(make_float3(pos.x, pos.y, pos.z));
	// examine neighbouring cells
	float3 force = make_float3(0.0f);

	for (int z = -1; z <= 1; z++) {
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {

				int3 neighbourPos =
					gridPos + make_int3(x, y, z);

				force +=
					collideCell(
						neighbourPos,
						index,
						pos,
						vel,
						oldPos,
						oldVel,
						cellStart,
						cellEnd
					);
			}
		}
	}

	uint originalIndex = gridParticleIndex[index];
	newVel[originalIndex] = make_float4(v_new + force, vel.w);
}

__global__
void calculate_forces(float4* d_b, float4* d_a, uint numParticles) {
	extern __shared__ float4 shPosition[];

	const uint body_id = blockIdx.x * blockDim.x + threadIdx.x;
	if (body_id >= numParticles) return;

	float4 myPosition = d_b[body_id];
	float3 acc = make_float3(0.0f, 0.0f, 0.0f);
	for (uint tile = 0; tile < gridDim.x; tile++) {
		uint idx = tile * blockDim.x + threadIdx.x;

		if (idx < numParticles)
			shPosition[threadIdx.x] = d_b[idx];
		else
			shPosition[threadIdx.x] = make_float4(0, 0, 0, 0);

		__syncthreads();
		acc = tile_calculation(myPosition, acc, tile, numParticles);
		__syncthreads();

	}
	d_a[body_id] = make_float4(acc.x, acc.y, acc.z, 0.0f);
}
///-----------------------------------------------------------------------------------------
/// </PARTICLE SYSTEM KERNEL>
///-----------------------------------------------------------------------------------------



///-----------------------------------------------------------------------------------------
/// <MARCHING CUBES DEVICE CONSTANTS / HELPERS>
///-----------------------------------------------------------------------------------------

// Optional constant-memory parameter block.
// Host side should use:
// cudaMemcpyToSymbol(mcParams, hostParams, sizeof(MarchingCubesParams));

__device__
uint mcClampu(uint v, uint hi) {
	return (v > hi) ? hi : v;
}

__device__
uint3 mcClampGridPos(int x, int y, int z, uint3 gridSize) {
	return make_uint3(
		static_cast<uint>(clipWithBounds(x, 0, static_cast<int>(gridSize.x) - 1)),
		static_cast<uint>(clipWithBounds(y, 0, static_cast<int>(gridSize.y) - 1)),
		static_cast<uint>(clipWithBounds(z, 0, static_cast<int>(gridSize.z) - 1))
	);
}

__device__
uint mcVolumeIndex(uint3 p, uint3 gridSize) {
	return p.x +
		p.y * gridSize.x +
		p.z * gridSize.x * gridSize.y;
}

__device__
float mcSampleVolumeFloat(
	float* volume,
	uint3 p,
	uint3 gridSize) {

	p.x = mcClampu(p.x, gridSize.x - 1);
	p.y = mcClampu(p.y, gridSize.y - 1);
	p.z = mcClampu(p.z, gridSize.z - 1);

	const uint i = mcVolumeIndex(p, gridSize);
	return volume[i];
}

__device__
float mcSampleVolumeFloatI(
	float* volume,
	int x,
	int y,
	int z,
	uint3 gridSize) {

	const uint3 p = mcClampGridPos(x, y, z, gridSize);
	return mcSampleVolumeFloat(volume, p, gridSize);
}

__device__
float mcSampleField(
	float* volume,
	uint3 gridPos,
	uint3 gridSize,
	float3 voxelSize) {

	(void)voxelSize;

	// EucliGen3D integration path:
	// sample the float scalar field generated by volumeKernelLauncher().
	return mcSampleVolumeFloat(volume, gridPos, gridSize);
}

__device__
float4 mcSampleField4(
	float* volume,
	uint3 gridPos,
	uint3 gridSize,
	float3 voxelSize) {

	(void)voxelSize;

	const int x = static_cast<int>(gridPos.x);
	const int y = static_cast<int>(gridPos.y);
	const int z = static_cast<int>(gridPos.z);

	const float v = mcSampleVolumeFloatI(volume, x, y, z, gridSize);

	// Central-difference gradient for normals.
	const float dx =
		mcSampleVolumeFloatI(volume, x + 1, y, z, gridSize) -
		mcSampleVolumeFloatI(volume, x - 1, y, z, gridSize);

	const float dy =
		mcSampleVolumeFloatI(volume, x, y + 1, z, gridSize) -
		mcSampleVolumeFloatI(volume, x, y - 1, z, gridSize);

	const float dz =
		mcSampleVolumeFloatI(volume, x, y, z + 1, gridSize) -
		mcSampleVolumeFloatI(volume, x, y, z - 1, gridSize);

	return make_float4(dx, dy, dz, v);
}

__device__
float mcGridPlaneDistance(float v, float period) {
	float m = fmodf(fabsf(v), period);
	return fminf(m, period - m);
}

__device__
float mcVoxelGridMask(float3 pos, int3 volSize) {
	// Convert centered volume coordinates:
	// [-64,  +64] -> [0, 128]
	float3 center = make_float3(
		volSize.x * 0.5f,
		volSize.y * 0.5f,
		volSize.z * 0.5f
	);

	float3 q = pos + center;

	const float period = static_cast<float>(MC_GRID_MAJOR_EVERY);

	float dx = mcGridPlaneDistance(q.x, period);
	float dy = mcGridPlaneDistance(q.y, period);
	float dz = mcGridPlaneDistance(q.z, period);

	bool xPlane = dx < MC_GRID_THICKNESS;
	bool yPlane = dy < MC_GRID_THICKNESS;
	bool zPlane = dz < MC_GRID_THICKNESS;

	// A wire line is the intersection of two grid planes.
	if ((xPlane && yPlane) ||
		(xPlane && zPlane) ||
		(yPlane && zPlane)) {
		return 1.0f;
	}

	return 0.0f;
}

__device__
uint3 mcCalcGridPos(
	uint i,
	uint3 gridSizeShift,
	uint3 gridSizeMask) {
	uint3 gridPos;

	gridPos.x = i & gridSizeMask.x;
	gridPos.y = (i >> gridSizeShift.y) & gridSizeMask.y;
	gridPos.z = (i >> gridSizeShift.z) & gridSizeMask.z;

	return gridPos;
}

__device__
uint3 mcAddOffset(
	uint3 p,
	uint x,
	uint y,
	uint z) {

	return make_uint3(p.x + x, p.y + y, p.z + z);
}

__device__
uint mcBuildCubeIndex(
	const float field[8],
	float isoValue) {

	uint cubeIndex = 0;

	if (field[0] < isoValue) cubeIndex |= 1;
	if (field[1] < isoValue) cubeIndex |= 2;
	if (field[2] < isoValue) cubeIndex |= 4;
	if (field[3] < isoValue) cubeIndex |= 8;
	if (field[4] < isoValue) cubeIndex |= 16;
	if (field[5] < isoValue) cubeIndex |= 32;
	if (field[6] < isoValue) cubeIndex |= 64;
	if (field[7] < isoValue) cubeIndex |= 128;

	return cubeIndex;
}

__device__
float3 mcVertexInterp(
	float isoLevel,
	float3 p0, float3 p1,
	float f0, float f1) {

	const float denom = f1 - f0;
	const float t =
		(fabsf(denom) > MC_EPSILON)
		? (isoLevel - f0) / denom
		: 0.5f;

	return lerp(p0, p1, t);
}

__device__
void mcVertexInterpWithNormal(
	float isoLevel,
	float3 p0,
	float3 p1,
	float4 f0,
	float4 f1,
	float3& p,
	float3& n) {

	const float denom = f1.w - f0.w;
	const float t =
		(fabsf(denom) > MC_EPSILON)
		? (isoLevel - f0.w) / denom
		: 0.5f;

	p = lerp(p0, p1, t);

	n.x = lerp(f0.x, f1.x, t);
	n.y = lerp(f0.y, f1.y, t);
	n.z = lerp(f0.z, f1.z, t);

	if (length(n) > MC_EPSILON) {
		n = normalize(n);
	}
	else {
		n = make_float3(0.0f, 0.0f, 1.0f);
	}
}

__device__
float3 mcVoxelToWorkspace(
	uint3 gridPos,
	float3 voxelSize) {

	return make_float3(
		-MC_WORKSPACE_HALF_EXTENT + gridPos.x * voxelSize.x,
		-MC_WORKSPACE_HALF_EXTENT + gridPos.y * voxelSize.y,
		-MC_WORKSPACE_HALF_EXTENT + gridPos.z * voxelSize.z
	);
}

__device__
uchar4 overlayMCVoxelGrid(
	uchar4 base,
	int3 volSize,
	Ray boxRay) {

	float accum = 0.0f;

	for (int i = 0; i < MC_GRID_STEPS; i++) {
		float t = static_cast<float>(i) /
			static_cast<float>(MC_GRID_STEPS - 1);

		float3 p = paramRay(boxRay, t);

		accum = fmaxf(
			accum,
			mcVoxelGridMask(p, volSize)
		);
	}

	if (accum <= 0.0f) return base;

	// Soft white-blue grid overlay
	const float a = 0.18f * accum;

	float r = static_cast<float>(base.x);
	float g = static_cast<float>(base.y);
	float b = static_cast<float>(base.z);

	r = r * (1.0f - a) + 210.0f * a;
	g = g * (1.0f - a) + 230.0f * a;
	b = b * (1.0f - a) + 255.0f * a;

	return make_uchar4(
		clip(static_cast<int>(r)),
		clip(static_cast<int>(g)),
		clip(static_cast<int>(b)),
		255
	);
}

__device__
uchar4 rayCastShaderMC(
	float* d_vol,
	int3 volSize,
	Ray boxRay,
	float dist) {

	uchar4 shade = rayCastShaderForMC(
		d_vol,
		volSize,
		boxRay,
		dist
	);

	// Empty-space / miss background.
	if (shade.w == 0) {
		return make_uchar4(8, 4, 24, 255);
	}

	// Convert the normal raycast red intensity into TEST-style cyan/white.
	const float a =
		fminf(fmaxf(static_cast<float>(shade.x) / 255.0f, 0.0f), 1.0f);

	return make_uchar4(
		clip(static_cast<int>(120.0f + 115.0f * a)),
		clip(static_cast<int>(220.0f + 35.0f * a)),
		255,
		255
	);
}

/*
__device__
uchar4 rayCastShaderMC(
	float* d_vol,
	int3 volSize,
	Ray boxRay,
	float dist) {

	uchar4 shade = rayCastShader(
		d_vol,
		volSize,
		boxRay,
		dist
	);

	// If this is still the empty-volume purple background,
	// darken it for voxel-grid contrast.
	if (shade.w == 0) {
		shade = make_uchar4(18, 8, 42, 0);
	}

	return shade;
}
*/


///-----------------------------------------------------------------------------------------
/// </MARCHING CUBES DEVICE CONSTANTS / HELPERS>
///-----------------------------------------------------------------------------------------




///-----------------------------------------------------------------------------------------
/// <VOLUME FIELD GENERATION>
///-----------------------------------------------------------------------------------------
__global__
void renderKernel(
	uchar4* d_out, float* d_vol,
	int w, int h,
	int3 volSize, int method,
	float zs, float theta, float phi,
	float threshold, float dist,
	float tintR, float tintG, float tintB) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;

	if ((c >= static_cast<uint>(w)) ||
		(r >= static_cast<uint>(h))) {
		return;
	}

	const uint i = c + r * static_cast<uint>(w);

	const float fovY = 60.0f;

	const uchar4 background =
		(method == 3)
		? make_uchar4(8, 4, 24, 0)
		: make_uchar4(64, 0, 128, 0);

	float3 source = { 0.f, 0.f, zs };

	float3 dir = makePerspectiveRayDir(c, r, w, h, fovY);

	// apply viewing transformation: here rotate about y-axis
	source = xRotate(source, -phi);
	source = yRotate(source, -theta);

	dir = xRotate(dir, -phi);
	dir = yRotate(dir, -theta);

	// prepare inputs for ray-box intersection
	float t0, t1;
	const Ray pixRay = { source, dir };

	float3 center = {
		volSize.x / 2.0f,
		volSize.y / 2.0f,
		volSize.z / 2.0f
	};

	const float3 boxmin = -center;
	const float3 boxmax = {
		volSize.x - center.x,
		volSize.y - center.y,
		volSize.z - center.z
	};

	// perform ray-box intersection test
	const bool hitBox =
		intersectBox(pixRay, boxmin, boxmax, &t0, &t1);

	uchar4 shade;
	if (!hitBox) shade = background; // miss box => background color
	else {
		if (t0 < 0.0f) t0 = 0.f; // clamp to 0 to avoid looking backward

		// bounded by points where the ray enters and leaves the box
		const Ray boxRay = {
			paramRay(pixRay, t0),
			paramRay(pixRay, t1) - paramRay(pixRay, t0)
		};

		if (method == 1) {
			shade = sliceShader(
				d_vol,
				volSize,
				boxRay,
				threshold,
				dist,
				source
			);
		}
		else if (method == 2) {
			shade = rayCastShader(
				d_vol,
				volSize,
				boxRay,
				threshold,
				tintR,
				tintG,
				tintB
			);
		}
		else if (method == 3) {
			shade = rayCastShaderMC(
				d_vol,
				volSize,
				boxRay,
				threshold
			);

			shade = overlayMCVoxelGrid(
				shade,
				volSize,
				boxRay
			);
		}
		else {
			shade = volumeRenderShader(
				d_vol,
				volSize,
				boxRay,
				threshold,
				NUMSTEPS,
				tintR,
				tintG,
				tintB
			);
		}
	}

	d_out[i] = shade;
}

__global__
void renderKernelOverlay(
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

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;

	if (c >= static_cast<uint>(w) ||
		r >= static_cast<uint>(h))
		return;

	const uint i = c + r * static_cast<uint>(w);

	const float fovY = 60.0f;

	float3 source = { 0.0f, 0.0f, zs };
	float3 dir = makePerspectiveRayDir(c, r, w, h, fovY);

	source = xRotate(source, -phi);
	source = yRotate(source, -theta);

	dir = xRotate(dir, -phi);
	dir = yRotate(dir, -theta);

	float3 center = {
		volSize.x / 2.0f,
		volSize.y / 2.0f,
		volSize.z / 2.0f
	};

	const float3 boxmin = -center;
	const float3 boxmax = {
		volSize.x - center.x,
		volSize.y - center.y,
		volSize.z - center.z
	};

	float t0 = 0.0f;
	float t1 = 0.0f;
	const Ray pixRay = { source, dir };
	if (!intersectBox(pixRay, boxmin, boxmax, &t0, &t1))
		return;

	if (t0 < 0.0f) t0 = 0.0f;

	const Ray boxRay = {
		paramRay(pixRay, t0),
		paramRay(pixRay, t1) - paramRay(pixRay, t0)
	};

	uchar4 shade = make_uchar4(0, 0, 0, 0);
	if (method == 1) {
		shade = sliceShader(
			d_vol,
			volSize,
			boxRay,
			threshold,
			dist,
			source
		);
	}
	else if (method == 2) {
		shade = rayCastShader(
			d_vol,
			volSize,
			boxRay,
			threshold,
			tintR,
			tintG,
			tintB
		);
	}
	else if (method == 3) {
		shade = rayCastShaderMC(
			d_vol,
			volSize,
			boxRay,
			threshold
		);

		shade = overlayMCVoxelGrid(
			shade,
			volSize,
			boxRay
		);
	}
	else {
		shade = volumeRenderShader(
			d_vol,
			volSize,
			boxRay,
			threshold,
			NUMSTEPS,
			tintR,
			tintG,
			tintB
		);
	}

	if (shade.w == 0) return;

	uchar4 base = d_out[i];
	overlayAlpha = fminf(fmaxf(overlayAlpha, 0.0f), 1.0f);

	// If the first pass missed geometry, write the brush directly
	if (base.w == 0) {
		d_out[i] = shade;
		return;
	}

	const float a = overlayAlpha;

	d_out[i] = make_uchar4(
		clip(static_cast<int>(
			static_cast<float>(base.x) * (1.0f - a) +
			static_cast<float>(shade.x) * a
		)),
		clip(static_cast<int>(
			static_cast<float>(base.y) * (1.0f - a) +
			static_cast<float>(shade.y) * a
		)),
		clip(static_cast<int>(
			static_cast<float>(base.z) * (1.0f - a) +
			static_cast<float>(shade.z) * a
		)),
		255
	);
}

__global__
void volumeKernel(
	float* d_vol,
	int3 volSize,
	int id,
	float4 param,
	float3 offset,
	float3 basisX,
	float3 basisY,
	float3 basisZ) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x; // column
	const uint r = blockIdx.y * blockDim.y + threadIdx.y; // row
	const uint s = blockIdx.z * blockDim.z + threadIdx.z; // stack

	if (c >= static_cast<unsigned int>(volSize.x) ||
		r >= static_cast<unsigned int>(volSize.y) ||
		s >= static_cast<unsigned int>(volSize.z)) {
		return;
	}

	const uint w = static_cast<unsigned int>(volSize.x);
	const uint h = static_cast<unsigned int>(volSize.y);

	const uint i = c + r * w + s * w * h;

	// compute and store
	d_vol[i] = funcSDFOffset(
		static_cast<int>(c),
		static_cast<int>(r),
		static_cast<int>(s),
		id,
		volSize,
		param,
		basisX,
		basisY,
		basisZ,
		offset
	);
}

__global__
void mirroredVolumeKernel(
	float* d_vol,
	int3 volSize,
	int id,
	float4 param,
	float3 offset,
	float3 basisX,
	float3 basisY,
	float3 basisZ,
	float3 mirrorNormal) {
	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;
	const uint s = blockIdx.z * blockDim.z + threadIdx.z;
	if (c >= static_cast<unsigned int>(volSize.x) ||
		r >= static_cast<unsigned int>(volSize.y) ||
		s >= static_cast<unsigned int>(volSize.z)) return;

	const float3 planeCenter = make_float3(
		0.5f * static_cast<float>(volSize.x),
		0.5f * static_cast<float>(volSize.y),
		0.5f * static_cast<float>(volSize.z));
	const float3 sample = make_float3(
		static_cast<float>(c), static_cast<float>(r), static_cast<float>(s));
	const float3 relative = sample - planeCenter;
	const float signedDistance = dot(relative, mirrorNormal);
	const float3 reflectedSample = sample - 2.0f * signedDistance * mirrorNormal;
	const float3 objectCenter = planeCenter + offset;
	const float3 worldPoint = reflectedSample - objectCenter;
	const float3 localPoint = worldPointToObjectLocalBasis(
		worldPoint, basisX, basisY, basisZ);

	const uint w = static_cast<unsigned int>(volSize.x);
	const uint h = static_cast<unsigned int>(volSize.y);
	const uint i = c + r * w + s * w * h;
	d_vol[i] = funcSDFLocal(
		localPoint.x, localPoint.y, localPoint.z, id, param);
}

__global__
void clearVolumeKernel(
	float* d_vol,
	int3 volSize,
	float clearValue) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;
	const uint s = blockIdx.z * blockDim.z + threadIdx.z;

	if (c >= static_cast<uint>(volSize.x) ||
		r >= static_cast<uint>(volSize.y) ||
		s >= static_cast<uint>(volSize.z)) {
		return;
	}

	const uint w = static_cast<uint>(volSize.x);
	const uint h = static_cast<uint>(volSize.y);
	const uint i = c + r * w + s * w * h;

	d_vol[i] = clearValue;
}

__global__
void composeVolumePrimitiveKernel(
	float* d_base, float* d_out,
	int op, int id, int3 volSize,
	float3 offset, float4 param,
	float3 basisX, float3 basisY, float3 basisZ) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;
	const uint s = blockIdx.z * blockDim.z + threadIdx.z;

	if (c >= static_cast<uint>(volSize.x) ||
		r >= static_cast<uint>(volSize.y) ||
		s >= static_cast<uint>(volSize.z)) {
		return;
	}

	const uint w = static_cast<uint>(volSize.x);
	const uint h = static_cast<uint>(volSize.y);
	const uint i = c + r * w + s * w * h;

	const float baseD = d_base[i];

	const float brushD = funcSDFOffset(
		static_cast<int>(c),
		static_cast<int>(r),
		static_cast<int>(s),
		id, volSize, param,
		basisX, basisY, basisZ,
		offset
	);

	float resultD = baseD;

	if (op == 0) {
		// FUSE/ADD
		resultD = fminf(baseD, brushD);
	}
	else {
		// CUT/SUB
		resultD = fmaxf(baseD, -brushD);
	}

	d_out[i] = resultD;
}

__global__
void composeVolumeFieldsKernel(
	const float* d_base,
	const float* d_brush,
	float* d_out,
	int3 volSize,
	int op) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;
	const uint s = blockIdx.z * blockDim.z + threadIdx.z;

	if (c >= static_cast<uint>(volSize.x) ||
		r >= static_cast<uint>(volSize.y) ||
		s >= static_cast<uint>(volSize.z)) return;

	const uint w = static_cast<uint>(volSize.x);
	const uint h = static_cast<uint>(volSize.y);
	const uint i = c + r * w + s * w * h;

	const float baseD = d_base[i];
	const float brushD = d_brush[i];

	float resultD = baseD;

	if (op == 0) {
		// FUSE / ADD:
		// Union of anchor and brush.
		resultD = fminf(baseD, brushD);
	}
	else {
		// CUT / SUB:
		// Subtract brush from anchor.
		resultD = fmaxf(baseD, -brushD);
	}

	d_out[i] = resultD;
}

__global__
void transformVolumeFieldKernel(
	const float* source,
	float* destination,
	int3 volSize,
	float3 scale,
	float3 offset,
	float3 basisX,
	float3 basisY,
	float3 basisZ) {

	const uint c = blockIdx.x * blockDim.x + threadIdx.x;
	const uint r = blockIdx.y * blockDim.y + threadIdx.y;
	const uint s = blockIdx.z * blockDim.z + threadIdx.z;

	if (c >= static_cast<uint>(volSize.x) ||
		r >= static_cast<uint>(volSize.y) ||
		s >= static_cast<uint>(volSize.z)) {
		return;
	}

	const uint index =
		c +
		r * static_cast<uint>(volSize.x) +
		s * static_cast<uint>(volSize.x * volSize.y);

	const float3 worldPoint = make_float3(
		static_cast<float>(c) - 0.5f * static_cast<float>(volSize.x),
		static_cast<float>(r) - 0.5f * static_cast<float>(volSize.y),
		static_cast<float>(s) - 0.5f * static_cast<float>(volSize.z)
	);

	const float3 shiftedPoint =
		worldPoint - offset;

	const float3 localPoint =
		worldPointToObjectLocalBasis(
			shiftedPoint,
			basisX,
			basisY,
			basisZ
		);

	const float sx = fmaxf(scale.x, 0.001f);
	const float sy = fmaxf(scale.y, 0.001f);
	const float sz = fmaxf(scale.z, 0.001f);

	const float3 sourcePoint = make_float3(
		localPoint.x / sx,
		localPoint.y / sy,
		localPoint.z / sz
	);

	const float sampled =
		sampleVolumeFieldTrilinear(
			source,
			volSize,
			sourcePoint
		);

	// Exact for uniform scaling; conservative approximation
	// for nonuniform scaling. The zero isosurface remains correct.
	const float distanceScale = fminf(sx, fminf(sy, sz));

	destination[index] = sampled * distanceScale;
}
///-----------------------------------------------------------------------------------------
/// </VOLUME FIELD GENERATION>
///-----------------------------------------------------------------------------------------




// -----------------------------------------------------------------------------
// VOLUME BOUNDARY SENSOR HELPERS
// -----------------------------------------------------------------------------

__device__ __forceinline__
uint volumeBoundaryLinearIndex(int x, int y, int z, int3 volSize) {

	return static_cast<uint>(x) +
		static_cast<uint>(y) * static_cast<uint>(volSize.x) +
		static_cast<uint>(z) * static_cast<uint>(volSize.x * volSize.y);
}


__device__ __forceinline__
int2 volumeBoundaryFaceDimensions(int face, int3 volSize) {

	switch (face) {

	case VOLUME_BOUNDARY_NEG_X:
	case VOLUME_BOUNDARY_POS_X:
		// X face:
		//
		//     u = Y
		//     v = Z
		return make_int2(
			volSize.y,
			volSize.z
		);

	case VOLUME_BOUNDARY_NEG_Y:
	case VOLUME_BOUNDARY_POS_Y:
		// Y face:
		//
		//     u = X
		//     v = Z
		return make_int2(
			volSize.x,
			volSize.z
		);

	default:
	case VOLUME_BOUNDARY_NEG_Z:
	case VOLUME_BOUNDARY_POS_Z:
		// Z face:
		//
		//     u = X
		//     v = Y
		return make_int2(
			volSize.x,
			volSize.y
		);
	}
}


__device__ __forceinline__
uint volumeBoundarySampleIndex(int face,int u,int v,
	bool interiorSample, int3 volSize) {

	int x = 0;
	int y = 0;
	int z = 0;

	// One-voxel inward coordinates.
	//
	// A dimension of one cannot move inward, so it remains at zero.
	const int innerNegX =
		volSize.x > 1
		? 1
		: 0;

	const int innerPosX =
		volSize.x > 1
		? volSize.x - 2
		: 0;

	const int innerNegY =
		volSize.y > 1
		? 1
		: 0;

	const int innerPosY =
		volSize.y > 1
		? volSize.y - 2
		: 0;

	const int innerNegZ =
		volSize.z > 1
		? 1
		: 0;

	const int innerPosZ =
		volSize.z > 1
		? volSize.z - 2
		: 0;

	switch (face) {

		// -----------------------------------------------------------------
		// X faces:
		//
		//     u = Y
		//     v = Z
		// -----------------------------------------------------------------
	case VOLUME_BOUNDARY_NEG_X:
		x =
			interiorSample
			? innerNegX
			: 0;

		y = u;
		z = v;
		break;

	case VOLUME_BOUNDARY_POS_X:
		x =
			interiorSample
			? innerPosX
			: volSize.x - 1;

		y = u;
		z = v;
		break;

		// -----------------------------------------------------------------
		// Y faces:
		//
		//     u = X
		//     v = Z
		// -----------------------------------------------------------------
	case VOLUME_BOUNDARY_NEG_Y:
		x = u;

		y =
			interiorSample
			? innerNegY
			: 0;

		z = v;
		break;

	case VOLUME_BOUNDARY_POS_Y:
		x = u;

		y =
			interiorSample
			? innerPosY
			: volSize.y - 1;

		z = v;
		break;

		// -----------------------------------------------------------------
		// Z faces:
		//
		//     u = X
		//     v = Y
		// -----------------------------------------------------------------
	default:
	case VOLUME_BOUNDARY_NEG_Z:
		x = u;
		y = v;

		z =
			interiorSample
			? innerNegZ
			: 0;
		break;

	case VOLUME_BOUNDARY_POS_Z:
		x = u;
		y = v;

		z =
			interiorSample
			? innerPosZ
			: volSize.z - 1;
		break;
	}

	return volumeBoundaryLinearIndex(x, y, z, volSize);
}

__device__ __forceinline__
bool volumeBoundaryPairIsUnsafe(
	float boundaryDistance,
	float interiorDistance,
	float isoValue,
	float safetyBand) {

	// Invalid scalar-field values should never be treated as safe.
	if (!isfinite(boundaryDistance) ||
		!isfinite(interiorDistance)) {

		return true;
	}

	const float safeBand = fmaxf(safetyBand, 0.0f);

	// SDF convention:
	//
	//     distance < iso  -> inside geometry
	//     distance = iso  -> surface
	//     distance > iso  -> outside geometry
	//
	// If a boundary sample is inside, on the surface, or within the
	// requested positive safety band, this patch is unsafe.
	const bool boundaryInsideOrNear =
		boundaryDistance <= isoValue + safeBand;

	const float boundaryDelta = boundaryDistance - isoValue;
	const float interiorDelta = interiorDistance - isoValue;

	// Detect the surface crossing the edge between the outer sample
	// and its first inward neighbor.
	const bool crossesBoundaryLayer =
		(boundaryDelta <= 0.0f && interiorDelta >= 0.0f) ||
		(boundaryDelta >= 0.0f && interiorDelta <= 0.0f);

	return boundaryInsideOrNear || crossesBoundaryLayer;
}


// -----------------------------------------------------------------------------
// VOLUME BOUNDARY SENSOR KERNEL
// -----------------------------------------------------------------------------

__global__
void countVolumeInsideSamplesKernel(
	const float* d_volume,
	uint* d_insideSampleCount,
	int3 volSize,
	float isoValue) {

	const uint index =
		blockIdx.x * blockDim.x + threadIdx.x;

	const uint sampleCount =
		static_cast<uint>(volSize.x) *
		static_cast<uint>(volSize.y) *
		static_cast<uint>(volSize.z);

	if (index >= sampleCount) return;
	if (*d_insideSampleCount != 0) return;

	const float distance = d_volume[index];

	if (isfinite(distance) && distance <= isoValue) {
		// Overlap containment needs a presence bit, not a full voxel count.
		// Avoid serializing every interior sample on one counter.
		atomicExch(d_insideSampleCount, 1u);
	}
}

//
// One CUDA thread owns one visual boundary patch.
//
// Each patch checks four outer-face samples and the corresponding four
// one-voxel-inward samples. This is more robust than checking only one corner
// and still aligns with the existing 128 x 128 cage layout.
// -----------------------------------------------------------------------------

__global__
void classifyVolumeBoundaryKernel(
	const float* d_volume,
	uchar* d_boundaryMask,
	uint* d_unsafeCount,
	int3 volSize,
	uint faceStride,
	float isoValue,
	float safetyBand) {

	const uint slot = blockIdx.x * blockDim.x + threadIdx.x;

	const uint totalSlots =
		faceStride * static_cast<uint>(VOLUME_BOUNDARY_FACE_COUNT);

	if (slot >= totalSlots) return;

	const uint face = slot / faceStride;
	const uint localPatch = slot - face * faceStride;

	const int2 faceDimensions =
		volumeBoundaryFaceDimensions(static_cast<int>(face), volSize);

	const uint validPatchCount =
		static_cast<uint>(faceDimensions.x * faceDimensions.y);


	// Non-cubic volumes use the largest face as the common stride.
	// Any unused slots belonging to smaller faces remain explicitly safe.
	if (localPatch >= validPatchCount) {
		d_boundaryMask[slot] = 0;
		return;
	}

	const int u =
		static_cast<int>(localPatch % static_cast<uint>(faceDimensions.x));

	const int v =
		static_cast<int>(localPatch / static_cast<uint>(faceDimensions.x));


	// The positive neighbor is clamped on the last row/column.
	//
	// This preserves one mask slot per cage square while keeping all
	// scalar-field accesses valid.
	const int u1 =
		u + 1 < faceDimensions.x
		? u + 1
		: u;

	const int v1 =
		v + 1 < faceDimensions.y
		? v + 1
		: v;

	const int patchU[4] = { u, u1, u, u1 };
	const int patchV[4] = { v, v, v1, v1 };

	bool unsafe = false;

#pragma unroll
	for (int corner = 0; corner < 4; corner++) {

		const uint boundaryIndex =
			volumeBoundarySampleIndex(
				static_cast<int>(face),
				patchU[corner],
				patchV[corner],
				false,
				volSize
			);

		const uint interiorIndex =
			volumeBoundarySampleIndex(
				static_cast<int>(face),
				patchU[corner],
				patchV[corner],
				true,
				volSize
			);

		const float boundaryDistance = d_volume[boundaryIndex];
		const float interiorDistance = d_volume[interiorIndex];

		if (volumeBoundaryPairIsUnsafe(
			boundaryDistance,
			interiorDistance,
			isoValue,
			safetyBand)) {

			unsafe = true;
			break;
		}
	}

	d_boundaryMask[slot] =
		unsafe
		? static_cast<uchar>(1)
		: static_cast<uchar>(0);

	if (unsafe && d_unsafeCount) {

		atomicAdd(d_unsafeCount, 1u);
	}
}


///-----------------------------------------------------------------------------------------
/// <MARCHING CUBES KERNELS>
///-----------------------------------------------------------------------------------------

__global__
void mcClassifyVoxelKernel(
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

	const uint blockId = blockIdx.y * gridDim.x + blockIdx.x;
	const uint i = blockId * blockDim.x + threadIdx.x;

	if (i >= numVoxels) return;

	const uint3 gridPos =
		mcCalcGridPos(i, gridSizeShift, gridSizeMask);

	float field[8];

	field[0] = mcSampleField(volume, mcAddOffset(gridPos, 0, 0, 0), gridSize, voxelSize);
	field[1] = mcSampleField(volume, mcAddOffset(gridPos, 1, 0, 0), gridSize, voxelSize);
	field[2] = mcSampleField(volume, mcAddOffset(gridPos, 1, 1, 0), gridSize, voxelSize);
	field[3] = mcSampleField(volume, mcAddOffset(gridPos, 0, 1, 0), gridSize, voxelSize);
	field[4] = mcSampleField(volume, mcAddOffset(gridPos, 0, 0, 1), gridSize, voxelSize);
	field[5] = mcSampleField(volume, mcAddOffset(gridPos, 1, 0, 1), gridSize, voxelSize);
	field[6] = mcSampleField(volume, mcAddOffset(gridPos, 1, 1, 1), gridSize, voxelSize);
	field[7] = mcSampleField(volume, mcAddOffset(gridPos, 0, 1, 1), gridSize, voxelSize);

	const uint cubeIndex = mcBuildCubeIndex(field, isoValue);
	const uint numVerts = FETCH(numVertsTable, cubeIndex);

	voxelVerts[i] = numVerts;
	voxelOccupied[i] = (numVerts > 0) ? 1 : 0;
}

__global__
void mcCompactVoxelsKernel(
	uint* compactedVoxelArray,
	uint* voxelOccupied,
	uint* voxelOccupiedScan,
	uint numVoxels) {

	const uint blockId = blockIdx.y * gridDim.x + blockIdx.x;
	const uint i = blockId * blockDim.x + threadIdx.x;

	if (i >= numVoxels) return;

	if (voxelOccupied[i]) {
		compactedVoxelArray[voxelOccupiedScan[i]] = i;
	}
}

__global__
void mcGenerateTrianglesKernel(
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

	const uint blockId = blockIdx.y * gridDim.x + blockIdx.x;
	const uint i = blockId * blockDim.x + threadIdx.x;

	if (i >= activeVoxels) return;

#if SKIP_EMPTY_VOXELS
	const uint voxel = compactedVoxelArray[i];
#else
	const uint voxel = i;
#endif

	const uint3 gridPos =
		mcCalcGridPos(voxel, gridSizeShift, gridSizeMask);

	const float3 p = mcVoxelToWorkspace(gridPos, voxelSize);

	float3 v[8];
	v[0] = p;
	v[1] = p + make_float3(voxelSize.x, 0.0f, 0.0f);
	v[2] = p + make_float3(voxelSize.x, voxelSize.y, 0.0f);
	v[3] = p + make_float3(0.0f, voxelSize.y, 0.0f);
	v[4] = p + make_float3(0.0f, 0.0f, voxelSize.z);
	v[5] = p + make_float3(voxelSize.x, 0.0f, voxelSize.z);
	v[6] = p + make_float3(voxelSize.x, voxelSize.y, voxelSize.z);
	v[7] = p + make_float3(0.0f, voxelSize.y, voxelSize.z);

	float4 field[8];

	field[0] = mcSampleField4(volume, mcAddOffset(gridPos, 0, 0, 0), gridSize, voxelSize);
	field[1] = mcSampleField4(volume, mcAddOffset(gridPos, 1, 0, 0), gridSize, voxelSize);
	field[2] = mcSampleField4(volume, mcAddOffset(gridPos, 1, 1, 0), gridSize, voxelSize);
	field[3] = mcSampleField4(volume, mcAddOffset(gridPos, 0, 1, 0), gridSize, voxelSize);
	field[4] = mcSampleField4(volume, mcAddOffset(gridPos, 0, 0, 1), gridSize, voxelSize);
	field[5] = mcSampleField4(volume, mcAddOffset(gridPos, 1, 0, 1), gridSize, voxelSize);
	field[6] = mcSampleField4(volume, mcAddOffset(gridPos, 1, 1, 1), gridSize, voxelSize);
	field[7] = mcSampleField4(volume, mcAddOffset(gridPos, 0, 1, 1), gridSize, voxelSize);

	uint cubeIndex = 0;
	if (field[0].w < isoValue) cubeIndex |= 1;
	if (field[1].w < isoValue) cubeIndex |= 2;
	if (field[2].w < isoValue) cubeIndex |= 4;
	if (field[3].w < isoValue) cubeIndex |= 8;
	if (field[4].w < isoValue) cubeIndex |= 16;
	if (field[5].w < isoValue) cubeIndex |= 32;
	if (field[6].w < isoValue) cubeIndex |= 64;
	if (field[7].w < isoValue) cubeIndex |= 128;

#if USE_SHARED
	__shared__ float3 vertlist[12 * NTHREADS];
	__shared__ float3 normlist[12 * NTHREADS];

	mcVertexInterpWithNormal(isoValue, v[0], v[1], field[0], field[1], vertlist[threadIdx.x], normlist[threadIdx.x]);
	mcVertexInterpWithNormal(isoValue, v[1], v[2], field[1], field[2], vertlist[threadIdx.x + NTHREADS], normlist[threadIdx.x + NTHREADS]);
	mcVertexInterpWithNormal(isoValue, v[2], v[3], field[2], field[3], vertlist[threadIdx.x + (NTHREADS * 2)], normlist[threadIdx.x + (NTHREADS * 2)]);
	mcVertexInterpWithNormal(isoValue, v[3], v[0], field[3], field[0], vertlist[threadIdx.x + (NTHREADS * 3)], normlist[threadIdx.x + (NTHREADS * 3)]);

	mcVertexInterpWithNormal(isoValue, v[4], v[5], field[4], field[5], vertlist[threadIdx.x + (NTHREADS * 4)], normlist[threadIdx.x + (NTHREADS * 4)]);
	mcVertexInterpWithNormal(isoValue, v[5], v[6], field[5], field[6], vertlist[threadIdx.x + (NTHREADS * 5)], normlist[threadIdx.x + (NTHREADS * 5)]);
	mcVertexInterpWithNormal(isoValue, v[6], v[7], field[6], field[7], vertlist[threadIdx.x + (NTHREADS * 6)], normlist[threadIdx.x + (NTHREADS * 6)]);
	mcVertexInterpWithNormal(isoValue, v[7], v[4], field[7], field[4], vertlist[threadIdx.x + (NTHREADS * 7)], normlist[threadIdx.x + (NTHREADS * 7)]);

	mcVertexInterpWithNormal(isoValue, v[0], v[4], field[0], field[4], vertlist[threadIdx.x + (NTHREADS * 8)], normlist[threadIdx.x + (NTHREADS * 8)]);
	mcVertexInterpWithNormal(isoValue, v[1], v[5], field[1], field[5], vertlist[threadIdx.x + (NTHREADS * 9)], normlist[threadIdx.x + (NTHREADS * 9)]);
	mcVertexInterpWithNormal(isoValue, v[2], v[6], field[2], field[6], vertlist[threadIdx.x + (NTHREADS * 10)], normlist[threadIdx.x + (NTHREADS * 10)]);
	mcVertexInterpWithNormal(isoValue, v[3], v[7], field[3], field[7], vertlist[threadIdx.x + (NTHREADS * 11)], normlist[threadIdx.x + (NTHREADS * 11)]);

	__syncthreads();
#else
	float3 vertlist[12];
	float3 normlist[12];

	mcVertexInterpWithNormal(isoValue, v[0], v[1], field[0], field[1], vertlist[0], normlist[0]);
	mcVertexInterpWithNormal(isoValue, v[1], v[2], field[1], field[2], vertlist[1], normlist[1]);
	mcVertexInterpWithNormal(isoValue, v[2], v[3], field[2], field[3], vertlist[2], normlist[2]);
	mcVertexInterpWithNormal(isoValue, v[3], v[0], field[3], field[0], vertlist[3], normlist[3]);

	mcVertexInterpWithNormal(isoValue, v[4], v[5], field[4], field[5], vertlist[4], normlist[4]);
	mcVertexInterpWithNormal(isoValue, v[5], v[6], field[5], field[6], vertlist[5], normlist[5]);
	mcVertexInterpWithNormal(isoValue, v[6], v[7], field[6], field[7], vertlist[6], normlist[6]);
	mcVertexInterpWithNormal(isoValue, v[7], v[4], field[7], field[4], vertlist[7], normlist[7]);

	mcVertexInterpWithNormal(isoValue, v[0], v[4], field[0], field[4], vertlist[8], normlist[8]);
	mcVertexInterpWithNormal(isoValue, v[1], v[5], field[1], field[5], vertlist[9], normlist[9]);
	mcVertexInterpWithNormal(isoValue, v[2], v[6], field[2], field[6], vertlist[10], normlist[10]);
	mcVertexInterpWithNormal(isoValue, v[3], v[7], field[3], field[7], vertlist[11], normlist[11]);
#endif

	const uint numVerts = FETCH(numVertsTable, cubeIndex);

	for (uint vIndex = 0; vIndex < numVerts; ++vIndex) {
		const uint edge = FETCH(triTable, cubeIndex * 16 + vIndex);
		const uint outIndex = numVertsScanned[voxel] + vIndex;

		if (outIndex < maxVerts) {
#if USE_SHARED
			pos[outIndex] =
				make_float4(vertlist[(edge * NTHREADS) + threadIdx.x], 1.0f);

			norm[outIndex] =
				make_float4(normlist[(edge * NTHREADS) + threadIdx.x], 0.0f);
#else
			pos[outIndex] =
				make_float4(vertlist[edge], 1.0f);

			norm[outIndex] =
				make_float4(normlist[edge], 0.0f);
#endif
		}
	}
}

///-----------------------------------------------------------------------------------------
/// </MARCHING CUBES KERNELS>
///-----------------------------------------------------------------------------------------
#endif
