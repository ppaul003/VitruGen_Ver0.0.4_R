#ifndef VITRUGEN_WORKSPACE_CONTEXT_H
#define VITRUGEN_WORKSPACE_CONTEXT_H

class EuclidRenderer;
class TheArbiter;
class ViewPort;
class CameraProcessor;

struct WorkspaceFrameContext {
	
	float deltaTime = 0.0f;
	
	// Monotonic application time supplied by the host.
	// Workspaces can use this for deterministic animation.
	float elapsedTime = 0.0f;

	int viewportWidth = 1920;
	int viewportHeight = 1080;

	float thetaRad = 0.0f;
	float phiRad = 0.0f;

	float zoom = 1.0f;
	float particleWorkspaceZs = 256.0f;
	float volumeRenderZs = 256.0f;

	bool displayEnabled = true;
};

struct WorkspaceServices {

	// Non-owning host services
	EuclidRenderer* renderer = nullptr;
	TheArbiter* arbiter = nullptr;
	ViewPort* viewport = nullptr;
	CameraProcessor* camera = nullptr;
};

#endif
