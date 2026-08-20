#ifndef VITRUGEN_WORKSPACE_CONTEXT_H
#define VITRUGEN_WORKSPACE_CONTEXT_H

class EuclidRenderer;
class TheArbiter;
class ViewPort;

struct WorkspaceFrameContext {
	
	float deltaTime = 0.0f;

	int viewportWidth = 1920;
	int viewportHeight = 1080;

	float thetaRad = 0.0f;
	float phiRad = 0.0f;

	float zoom = 1.0f;

	bool displayEnabled = true;
};

struct WorkspaceServices {

	// Non-owning host services
	EuclidRenderer* renderer = nullptr;
	TheArbiter* arbiter = nullptr;
	ViewPort* viewport = nullptr;
};

#endif
