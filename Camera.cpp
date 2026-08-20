#include "Camera.h"

#include <GL/freeglut.h>
#include <cmath>

using namespace std;

static float clampCameraFloat(float v, float lo, float hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

CameraProcessor::CameraProcessor() :
	m_camera_trans{ 0.0f, 0.0f, -5.0f },
	m_camera_rot{ 0.0f, 0.0f, 0.0f },
	m_camera_trans_lag{ 0.0f, 0.0f, -5.0f },
	m_camera_rot_lag{ 0.0f, 0.0f, 0.0f },
	m_menuCubeRotation(0.0f) {}
CameraProcessor::~CameraProcessor() {}

void CameraProcessor::zoom(float amount) {
	m_camera_trans[2] +=
		amount * fabs(m_camera_trans[2]);
};
void CameraProcessor::orbit(float dx, float dy) {
	m_camera_rot[0] += dy / 5.0f;
	m_camera_rot[1] += dx / 5.0f;
};
void CameraProcessor::updateLag() {
	for (int c = 0; c < 3; c++) {
		m_camera_trans_lag[c] +=
			(m_camera_trans[c] - m_camera_trans_lag[c]) * kInertia;

		m_camera_rot_lag[c] +=
			(m_camera_rot[c] - m_camera_rot_lag[c]) * kInertia;
	}

	// Optional tactile camera bump when zoom reaches the SINGLE_PARTICLE limit.
	if (m_bumpT > 0.0f) {
		m_camera_trans_lag[2] -= 0.025f * m_bumpT;
		m_bumpT *= 0.82f;

		if (m_bumpT < 0.01f) {
			m_bumpT = 0.0f;
		}
	}
}
void CameraProcessor::applyMenuCameraTransform(float previewRotationDegrees) {
	// Ver0.0.2: menu preview is also camera-lagged, so returning from
	// deeper layers back to Layer 0 no longer hard-snaps the view.
	m_camera_trans[0] = 1.65f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -8.0f;

	m_camera_rot[0] = 18.0f;
	m_camera_rot[1] = previewRotationDegrees;
	m_camera_rot[2] = 0.0f;

	glTranslatef(
		m_camera_trans_lag[0],
		m_camera_trans_lag[1],
		m_camera_trans_lag[2]
	);

	glRotatef(m_camera_rot_lag[0], 1.0f, 0.0f, 0.0f);
	glRotatef(m_camera_rot_lag[1], 0.0f, 1.0f, 0.0f);
	glRotatef(m_camera_rot_lag[2], 0.0f, 0.0f, 1.0f);
}
void CameraProcessor::applyViewCameraTransform() {
	glTranslatef(
		m_camera_trans_lag[0],
		m_camera_trans_lag[1],
		m_camera_trans_lag[2]
	);

	glRotatef(m_camera_rot_lag[0], 1.0f, 0.0f, 0.0f);
	glRotatef(m_camera_rot_lag[1], 0.0, 1.0f, 0.0f);
}

// -- Camera math helpers ---
void CameraProcessor::xform(float* v, float* r, GLfloat* m) {
	r[0] = v[0] * m[0] + v[1] * m[4] + v[2] * m[8] + m[12];
	r[1] = v[0] * m[1] + v[1] * m[5] + v[2] * m[9] + m[13];
	r[2] = v[0] * m[2] + v[1] * m[6] + v[2] * m[10] + m[14];
}
void CameraProcessor::ixform(float* v, float* r, GLfloat* m) {
	r[0] = v[0] * m[0] + v[1] * m[1] + v[2] * m[2];
	r[1] = v[0] * m[4] + v[1] * m[5] + v[2] * m[6];
	r[2] = v[0] * m[8] + v[1] * m[9] + v[2] * m[10];
}
void CameraProcessor::ixformPoint(float* v, float* r, GLfloat* m) {
	float x[4];
	x[0] = v[0] - m[12];
	x[1] = v[1] - m[13];
	x[2] = v[2] - m[14];
	x[3] = 1.0f;
	ixform(x, r, m);
}

void CameraProcessor::focus3DFromMenu(float previewRotationDegrees) {
	// Start the lagged camera from the approximate menu-preview pose.
	m_camera_trans_lag[0] = 1.65f;
	m_camera_trans_lag[1] = 0.0;
	m_camera_trans_lag[2] = -8.0f;

	m_camera_rot_lag[0] = 18.0f;
	m_camera_rot_lag[1] = previewRotationDegrees;
	m_camera_rot_lag[2] = 0.0f;

	// Target final 3D view: centered, front-facing, stable.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0;
	m_camera_trans[2] = -5.0f;

	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}
void CameraProcessor::focusStandard3DView() {
	// Standard 3D workspace target.
	// Do not touch lag values here; updateLag() will smooth the movement.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -5.0f;

	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}
void CameraProcessor::focusSingleParticleConfigView() {
	// Zoomed-in SINGLE_PARTICLE configuration target.
	// This makes particle 0 easier to see without requiring mouse-wheel zoom.
	// Do not touch lag values here; updateLag() will smooth the movement.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -1.25f;

	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}

void CameraProcessor::setBehaviorMode(CameraBehaviorMode mode) {
	if (m_behaviorMode == mode) return;

	m_behaviorMode = mode;
	updateBehavior();
}
bool CameraProcessor::orbitEnabled() const {
	// Checkpoint 1:
	// Only the SINGLE_PARTICLE workplane-lock mode disables orbit.
	// Everything else behaves like the old camera.
	return !isWorkplaneLocked();
}

bool CameraProcessor::isSingleParticleCamera() const {
	return
		m_behaviorMode == CAM_SINGLE_PARTICLE_ORBIT_CLOSE ||
		m_behaviorMode == CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED ||
		m_behaviorMode == CAM_SINGLE_PARTICLE_VOLUME ||
		m_behaviorMode == CAM_SINGLE_PARTICLE_MARCHING_CUBES;
}
bool CameraProcessor::isWorkplaneLocked() const {
	return m_behaviorMode == CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED;
}

float CameraProcessor::adaptivePocketZoomStep(
	float currentZs,
	float minZs,
	float dampingStartZs,
	float baseStep,
	float minStep) const {

	if (dampingStartZs <= minZs) {
		return baseStep;
	}

	if (currentZs >= dampingStartZs) {
		return baseStep;
	}

	const float t = clampCameraFloat(
		(currentZs - minZs) / (dampingStartZs - minZs),
		0.0f,
		1.0f
	);

	// Smoothstep gives a gentle falloff as the camera enters the cube.
	const float smoothT = t * t * (3.0f - 2.0f * t);

	return minStep + (baseStep - minStep) * smoothT;
}

float CameraProcessor::getOrbitSensitivityScale() const {
	if (!isSingleParticleCamera()) {
		return 1.0f;
	}

	if (isWorkplaneLocked()) {
		return 0.0f;
	}

	const bool volumeLike =
		m_behaviorMode == CAM_SINGLE_PARTICLE_VOLUME ||
		m_behaviorMode == CAM_SINGLE_PARTICLE_MARCHING_CUBES;

	const float currentZs = volumeLike
		? m_volumeRenderZs
		: m_particleWorkspaceZs;

	const float minZs = volumeLike
		? kVolumeRenderZsMin
		: kParticleWorkspaceZsMin;

	const float dampingStartZs = volumeLike
		? kVolumeRenderDampingStartZs
		: kParticleWorkspaceDampingStartZs;

	if (currentZs >= dampingStartZs) {
		return 1.0f;
	}

	const float t = clampCameraFloat(
		(currentZs - minZs) / (dampingStartZs - minZs),
		0.0f,
		1.0f
	);

	const float smoothT = t * t * (3.0f - 2.0f * t);

	return kSingleParticleOrbitMinScale +
		(1.0f - kSingleParticleOrbitMinScale) * smoothT;
}

void CameraProcessor::updateBehavior() {
	switch (m_behaviorMode) {
	case CAM_MENU_PREVIEW:
		// Menu preview still receives the live preview rotation from
		// applyMenuCameraTransform(...). This is only the default target.
		m_camera_trans[0] = 1.65f;
		m_camera_trans[1] = 0.0f;
		m_camera_trans[2] = -8.0f;

		m_camera_rot[0] = 18.0f;
		m_camera_rot[1] = 0.0f;
		m_camera_rot[2] = 0.0f;
		break;

	case CAM_SINGLE_PARTICLE_ORBIT_CLOSE:
		setTargetSingleParticleClose();
		break;

	case CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED:
		setTargetSingleParticleWorkplaneLocked();
		break;

	case CAM_SINGLE_PARTICLE_VOLUME:
		setTargetVolumeRender();
		break;

	case CAM_SINGLE_PARTICLE_MARCHING_CUBES:
		setTargetMarchingCubes();
		break;

	default:
	case CAM_STANDARD_3D_ORBIT:
		setTargetStandard3D();
		break;
	}
}

void CameraProcessor::setTargetStandard3D() {
	m_bumpT = 0.0f;
	focusStandard3DView();
}
void CameraProcessor::setTargetSingleParticleClose() {
	m_bumpT = 0.0f;
	m_minZoomZ = -1.25f;
	m_maxZoomZ = -8.0f;
	focusSingleParticleConfigView();
}
void CameraProcessor::setTargetSingleParticleWorkplaneLocked() {
	m_bumpT = 0.0f;

	m_minZoomZ = -1.25f;
	m_maxZoomZ = -8.0f;

	// Front-facing XY workplane view.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -1.75f;

	// Locked along the Z-axis.
	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}
void CameraProcessor::setTargetVolumeRender() {
	m_bumpT = 0.0f;

	m_minZoomZ = -1.25f;
	m_maxZoomZ = -8.0f;

	// Checkpoint 1:
	// Keep this close and stable for now.
	// Later we can special-case volumetric render if we want a hard snap.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -2.25f;

	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}
void CameraProcessor::setTargetMarchingCubes() {
	m_bumpT = 0.0f;

	m_minZoomZ = -1.25f;
	m_maxZoomZ = -8.0f;

	// Slightly farther than the particle-edit view so the generated
	// mesh / voxel diagnostics have breathing room.
	m_camera_trans[0] = 0.0f;
	m_camera_trans[1] = 0.0f;
	m_camera_trans[2] = -2.75f;

	m_camera_rot[0] = 0.0f;
	m_camera_rot[1] = 0.0f;
	m_camera_rot[2] = 0.0f;
}
void CameraProcessor::zoomByWheel(int wheelButton) {
	if (wheelButton != 3 && wheelButton != 4) return;

	const bool zoomIn = (wheelButton == 3);

	// Preserve old behavior outside SINGLE_PARTICLE camera modes.
	if (!isSingleParticleCamera()) {
		zoom(zoomIn ? 0.10f : -0.10f);
		return;
	}

	float z = m_camera_trans[2];

	const float collisionZ = m_minZoomZ;
	const float farZ = m_maxZoomZ;

	const float distanceFromCollision =
		fabs(z - collisionZ);

	float step = 0.06f * distanceFromCollision;

	if (step < 0.015f) {
		step = 0.015f;
	}

	if (step > 0.30f) {
		step = 0.30f;
	}

	if (zoomIn) {
		z += step;
	}
	else {
		z -= step;
	}

	// Closest allowed zoom.
	if (z > collisionZ) {
		z = collisionZ;
		m_bumpT = 1.0f;
	}

	// Farthest allowed zoom.
	if (z < farZ) {
		z = farZ;
	}

	m_camera_trans[2] = z;
}

void CameraProcessor::updatePocketZoomLag() {
	// --- OpenGL SINGLE_PARTICLE / workplane pocket distance ---
	m_particleWorkspaceZsTarget =
		clampCameraFloat(
			m_particleWorkspaceZsTarget,
			kParticleWorkspaceZsMin,
			kParticleWorkspaceZsMax
		);

	const float dzParticle =
		m_particleWorkspaceZsTarget -
		m_particleWorkspaceZs;

	m_particleWorkspaceZs +=
		dzParticle * kPocketZoomInertia;

	if (fabs(dzParticle) < 0.001f) {
		m_particleWorkspaceZs =
			m_particleWorkspaceZsTarget;
	}

	// --- CUDA VOLUME / MC ray-camera distance ---
	m_volumeRenderZsTarget =
		clampCameraFloat(
			m_volumeRenderZsTarget,
			kVolumeRenderZsMin,
			kVolumeRenderZsMax
		);

	const float dzVolume =
		m_volumeRenderZsTarget -
		m_volumeRenderZs;

	m_volumeRenderZs +=
		dzVolume * kPocketZoomInertia;

	if (fabs(dzVolume) < 0.001f) {
		m_volumeRenderZs =
			m_volumeRenderZsTarget;
	}
}
void CameraProcessor::zoomParticleWorkspaceByWheel(int wheelButton) {
	if (wheelButton != 3 && wheelButton != 4) return;

	const float step = adaptivePocketZoomStep(
		m_particleWorkspaceZsTarget,
		kParticleWorkspaceZsMin,
		kParticleWorkspaceDampingStartZs,
		kPocketZoomBaseStep,
		kPocketZoomMinStep
	);

	if (wheelButton == 3) {
		// Wheel up: move closer.
		m_particleWorkspaceZsTarget -= step;
	}
	else {
		// Wheel down: move farther.
		m_particleWorkspaceZsTarget += step;
	}

	m_particleWorkspaceZsTarget =
		clampCameraFloat(
			m_particleWorkspaceZsTarget,
			kParticleWorkspaceZsMin,
			kParticleWorkspaceZsMax
		);
}
void CameraProcessor::zoomVolumeRenderByWheel(int wheelButton) {
	if (wheelButton != 3 && wheelButton != 4) return;

	const float step = adaptivePocketZoomStep(
		m_volumeRenderZsTarget,
		kVolumeRenderZsMin,
		kVolumeRenderDampingStartZs,
		kPocketZoomBaseStep,
		kPocketZoomMinStep
	);

	if (wheelButton == 3) {
		// Wheel up: move closer through the CUDA ray camera.
		m_volumeRenderZsTarget -= step;
	}
	else {
		// Wheel down: move farther through the CUDA ray camera.
		m_volumeRenderZsTarget += step;
	}

	m_volumeRenderZsTarget =
		clampCameraFloat(
			m_volumeRenderZsTarget,
			kVolumeRenderZsMin,
			kVolumeRenderZsMax
		);
}

void CameraProcessor::resetParticleWorkspaceZoom() {
	m_particleWorkspaceZs =
		kParticleWorkspaceZsDefault;

	m_particleWorkspaceZsTarget =
		kParticleWorkspaceZsDefault;
}
void CameraProcessor::resetVolumeRenderZoom() {
	m_volumeRenderZs =
		kVolumeRenderZsDefault;

	m_volumeRenderZsTarget =
		kVolumeRenderZsDefault;
}
void CameraProcessor::resetPocketZooms() {
	resetParticleWorkspaceZoom();
	resetVolumeRenderZoom();
}