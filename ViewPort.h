#ifndef VIEWPORT_H
#define VIEWPORT_H

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <algorithm>
#include <string>

#include "WorkspacePresentation.h"

class ViewPort {
public:
	enum class ObjExportPanelMode {
		HIDDEN = 0,

		SELECT,
		CONFIRM,
		WORKING,
		COMPLETE,
		FAILED
	};

	struct ObjExportPanelData {

		ObjExportPanelMode mode =
			ObjExportPanelMode::HIDDEN;

		bool yesSelected = true;

		int progressPercent = 0;
		int spinnerFrame = 0;

		std::string statusText;
		std::vector<std::string> logLines;

		std::string titleText;
		std::string confirmText;
		std::vector<std::string> selectionLines;

		int selectedIndex = 0;
	};

public:
	// =========================================================
	// LIFECYCLE
	// =========================================================
	ViewPort();
	~ViewPort();

	// =========================================================
	// VIEWPORT / PROJECTION
	//
	// These remain compatible with the existing EuclidEngine
	// call pattern.
	// =========================================================
	void resize(int width, int height);
	void applyPerspective(float fovDegrees = 60.0f);

	// =========================================================
	// 2D OVERLAY CONTEXT
	//
	// Generic screen-space drawing helpers.
	// =========================================================
	void beginOverlay2D();
	void endOverlay2D();

	void drawText2D(
		float x,
		float y,
		const char* text,
		void* font = GLUT_BITMAP_HELVETICA_18
	);

	// =========================================================
	// GENERIC PRESENTATION
	//
	// ViewPort does NOT determine:
	//
	//     active workspace
	//     active layer
	//     selected workspace item
	//     workspace state
	//
	// It receives already-prepared presentation data and
	// renders it using the common VitruGen UI shell.
	// =========================================================
	void drawOverlay(
		const WorkspacePresentation& presentation,
		const ObjExportPanelData* modalData = nullptr
	);

	// =========================================================
	// VIEWPORT QUERY
	// =========================================================
	int getWidth() const { return m_windowWidth; }
	int getHeight() const { return m_windowHeight; }
	float getAspect() const;

	void updateSubLayerPanelAnimation(bool visible);
	void drawSubLayerPresentation(const WorkspacePresentation& presentation);

private:
	// =========================================================
	// GENERIC INTERNAL HELPERS
	// =========================================================
	static int clampPositive(int value) { return std::max(1, value); }
	void updatePanelAnimation(bool visible);

	// ---------------------------------------------------------
	// Global VitruGen presentation frame.
	// ---------------------------------------------------------
	void drawWorkspaceFrame(
		float alpha,
		const char* label = nullptr
	);

	// ---------------------------------------------------------
	// Main generic information/control panel.
	// ---------------------------------------------------------
	void drawPanelBackground();
	void drawPresentationHeader(const WorkspacePresentation& presentation);
	void drawPresentationSections(const WorkspacePresentation& presentation);
	void drawPresentationFooter(const WorkspacePresentation& presentation);
	void drawRuntimeStatusPanel(const WorkspaceRuntimeStatus& status);

	// ---------------------------------------------------------
	// Generic row rendering.
	//
	// WorkspacePresentation supplies meaning.
	// ViewPort supplies appearance.
	// ---------------------------------------------------------
	void drawPanelRow(
		float x,
		float y,
		const WorkspacePanelRow& row
	);

	void drawSectionDivider(float y);
	
	// =========================================================
	// PANEL POSITION
	// =========================================================
	float panelOffsetX() const;
	float panelX(float x) const { return x + panelOffsetX(); }

	void drawObjExportPanel(const ObjExportPanelData& data);

private:
	// =========================================================
	// VIEWPORT STATE
	// =========================================================
	int m_windowWidth = 1920;
	int m_windowHeight = 1080;

	float m_fov = 60.0f;

	// =========================================================
	// GENERIC VITRUGEN UI LAYOUT
	//
	// These intentionally preserve the visual proportions of
	// the existing Ver004 interface while removing workspace
	// semantics from ViewPort.
	// =========================================================
	float m_panelWidth = 650.0f;
	float m_margin = 42.0f;

	float m_headerHeight = 140.0f;

	float m_sectionSpacing = 42.0f;
	float m_rowSpacing = 55.0f;

	// =========================================================
	// PANEL ANIMATION
	//
	// 1.0 = fully visible
	// 0.0 = fully hidden
	// =========================================================
	float m_panelSlide = 1.0f;
	float m_subLayerPanelSlide = 0.0f;

};


#endif
