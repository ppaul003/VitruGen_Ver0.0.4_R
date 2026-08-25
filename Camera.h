#ifndef CAMERA_PROCESS_H
#define CAMERA_PROCESS_H

#include <GL/glew.h>
#include <cmath>

class CameraProcessor {
public:
	enum CameraBehaviorMode {
		CAM_MENU_PREVIEW = 0,

		CAM_STANDARD_3D_ORBIT,

		// SINGLE_PARTICLE OpenGL / particle-anchor workspace.
		CAM_SINGLE_PARTICLE_ORBIT_CLOSE,

		// SINGLE_PARTICLE sub-layer 1: XY workplane selection.
		CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED,

		// SINGLE_PARTICLE sub-layer 2: volume render.
		CAM_SINGLE_PARTICLE_VOLUME,

		// SINGLE_PARTICLE sub-layer 3: marching cubes.
		CAM_SINGLE_PARTICLE_MARCHING_CUBES
	};

	CameraProcessor();
	~CameraProcessor();

	void zoom(float amount);

	void orbit(float dx, float dy);

	void updateLag();
	void applyMenuCameraTransform(float previewRotationDegrees);
	void applyViewCameraTransform();

	float* getTranslation() { return m_camera_trans; };
	float* getRotation() { return m_camera_rot; };
	float* getLaggedTranslation() { return m_camera_trans_lag; };
	float* getLaggedRotation() { return m_camera_rot_lag; };
	const float* getLaggedRotation() const { return m_camera_rot_lag; };

	// --- CAMERA TRANSFORMS ---
	static void xform(float* v, float* r, GLfloat* m);
	static void ixform(float* v, float* r, GLfloat* m);
	static void ixformPoint(float* v, float* r, GLfloat* m);

	void focus3DFromMenu(float previewRotationDegrees);
	void focusStandard3DView();
	void focusSingleParticleConfigView();

	void setBehaviorMode(CameraBehaviorMode mode);
	CameraBehaviorMode getBehaviorMode() const { return m_behaviorMode; }

	bool orbitEnabled() const;
	bool isSingleParticleCamera() const;
	bool isWorkplaneLocked() const;
	float getOrbitSensitivityScale() const;

	void updateBehavior();
	void setTargetStandard3D();
	void setTargetSingleParticleClose();
	void setTargetSingleParticleWorkplaneLocked();
	void setTargetVolumeRender();
	void setTargetMarchingCubes();

	void zoomByWheel(int wheelButton);

	// --- SINGLE_PARTICLE pocket / volume zoom distances ---
	void updatePocketZoomLag();

	float getParticleWorkspaceZs() const { return m_particleWorkspaceZs; }
	float getVolumeRenderZs() const { return m_volumeRenderZs; }

	void zoomParticleWorkspaceByWheel(int wheelButton);
	void zoomVolumeRenderByWheel(int wheelButton);

	void resetParticleWorkspaceZoom();
	void resetVolumeRenderZoom();
	void resetPocketZooms();

	float adaptivePocketZoomStep(
		float currentZs,
		float minZs,
		float dampingStartZs,
		float baseStep,
		float minStep
	) const;

private:
	// --- SINGLE_PARTICLE local pocket camera distances ---
	static constexpr float kPocketZoomInertia = 0.10f;

	static constexpr float kParticleWorkspaceZsDefault = 256.0f;
	//static constexpr float kParticleWorkspaceZsDefault = 80.0f;
	// `particleWorkspaceZs` is converted by EuclidRenderer into an OpenGL
	// camera distance. With the current renderer mapping:
	//      distance ~= 4.0f * (zs / 256.0f)
	// so zs=128 enters the 4x4 workspace cube, and zs=48 gives a close but
	// still safe inspection distance around the particle/mesh anchor.
	static constexpr float kParticleWorkspaceZsMin = 32.0f;
	static constexpr float kParticleWorkspaceZsMax = 1024.0f;

	static constexpr float kVolumeRenderZsDefault = 256.0f;
	static constexpr float kVolumeRenderZsMin = 1.0f;
	static constexpr float kVolumeRenderZsMax = 1024.0f;

	static constexpr float kPocketZoomBaseStep = 7.0f;
	static constexpr float kPocketZoomMinStep = 0.35f;

	static constexpr float kParticleWorkspaceDampingStartZs = 128.0f;
	static constexpr float kVolumeRenderDampingStartZs = 128.0f;

	static constexpr float kSingleParticleOrbitMinScale = 0.22f;

	CameraBehaviorMode m_behaviorMode = CAM_STANDARD_3D_ORBIT;

	static constexpr float kInertia = 0.1f;

	float m_camera_trans[3];
	float m_camera_rot[3];

	float m_camera_trans_lag[3];
	float m_camera_rot_lag[3];

	float m_menuCubeRotation;

	float m_minZoomZ = -1.25f;
	float m_maxZoomZ = -12.0f;
	float m_bumpT = 0.0f;

	float m_particleWorkspaceZs =
		kParticleWorkspaceZsDefault;

	float m_particleWorkspaceZsTarget =
		kParticleWorkspaceZsDefault;

	float m_volumeRenderZs =
		kVolumeRenderZsDefault;

	float m_volumeRenderZsTarget =
		kVolumeRenderZsDefault;
};

#endif