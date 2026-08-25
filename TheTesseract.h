#ifndef _THE_TESSERACT_H_
#define _THE_TESSERACT_H_

#include <GL/glew.h>
#include <cstddef>
#include <vector>
#include <vector_types.h>
#include <filesystem>

#include "IWorkspace.h"
#include "WorkspaceContext.h"
#include "WorkspaceInput.h"
#include "WorkspacePresentation.h"

#include "TheArbiter.h"
#include "renderer_Euclid.h"

#include "DiagnosticIdle.h"
#include "ParticleSimWorkspace.h"

struct cudaGraphicsResource;

class Tesseract {
public:
	bool initialize(WorkspaceServices services);

	void update(const WorkspaceFrameContext& frame);
	void render(const WorkspaceFrameContext& frame);

	bool handleInput(const WorkspaceInputEvent& event);

	WorkspacePresentation
		presentation() const;
	
private:
	WorkspaceServices m_services;

	DiagnosticIdle m_diagnosticIdle;
	ParticleSimWorkspace m_particleSimWorkspace;

	IWorkspace* m_activeWorkspace = nullptr;
};

#endif
