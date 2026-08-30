#include "ViewPort.h"

#include <algorithm>
#include <string>
#include <cstdio>
#include <chrono>
#include <cmath>

using namespace std;

ViewPort::ViewPort() {
}

ViewPort::~ViewPort() {
}

void ViewPort::resize(int width, int height) {

	m_windowWidth = clampPositive(width);
	m_windowHeight = clampPositive(height);

	glViewport(0, 0, m_windowWidth, m_windowHeight);
}

// =============================================================================
// 3D PROJECTION
// =============================================================================
void ViewPort::applyPerspective(float fovDegrees) {
	m_fov = fovDegrees;

	glViewport(0, 0, m_windowWidth, m_windowHeight);
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	gluPerspective(
		m_fov,
		getAspect(),
		0.01,
		100.0
	);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

// =============================================================================
// BASIC VIEWPORT
// =============================================================================
float ViewPort::getAspect() const {
	if (m_windowHeight <= 0)
		return 1.0f;

	return
		static_cast<float>(m_windowWidth) /
		static_cast<float>(m_windowHeight);
}

// =============================================================================
// 2D OVERLAY CONTEXT
// =============================================================================
void ViewPort::beginOverlay2D() {

	glViewport(0, 0, m_windowWidth, m_windowHeight);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	gluOrtho2D(
		0.0,
		static_cast<double>(m_windowWidth),
		static_cast<double>(m_windowHeight),
		0.0
	);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}

void ViewPort::endOverlay2D() {

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glMatrixMode(GL_MODELVIEW);
}

// =============================================================================
// TEXT
// =============================================================================
void ViewPort::drawText2D(
	float x,
	float y,
	const char* text,
	void* font) {

	if (!text) return;
	glRasterPos2f(x, y);

	for (const char* p = text; *p; p++) {

		glutBitmapCharacter(font, *p);
	}
}

// =============================================================================
// PANEL ANIMATION
// =============================================================================
void ViewPort::updatePanelAnimation(bool visible) {

	const float target =
		visible
		? 1.0f
		: 0.0f;

	m_panelSlide +=
		(target - m_panelSlide) * 0.15f;

	if (m_panelSlide < 0.001f)
		m_panelSlide = 0.0f;

	if (m_panelSlide > 0.999f)
		m_panelSlide = 1.0f;
}

float ViewPort::panelOffsetX() const {

	const float hiddenX =
		-(m_panelWidth + m_margin + 24.0f);

	return hiddenX * (1.0f - m_panelSlide);	
}

// =============================================================================
// WORKSPACE FRAME
// =============================================================================
void ViewPort::drawWorkspaceFrame(
	float alpha,
	WorkspaceStatusTone tone,
	bool blink,
	const char* label) {

	const float margin = 24.0f;
	const float x0 = margin;
	const float y0 = margin;

	const float x1 =
		static_cast<float>(m_windowWidth) -
		margin;

	const float y1 =
		static_cast<float>(m_windowHeight) -
		margin;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glUseProgram(0);
	glEnable(GL_BLEND);

	glBlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	// =========================================================
	// FRAME PRESENTATION
	// =========================================================
	float frameAlpha = alpha;

	float r = 0.85f;
	float g = 0.95f;
	float b = 1.00f;

	switch (tone) {

	// -----------------------------------------------------
	// AUTO transition
	// -----------------------------------------------------
	case WorkspaceStatusTone::Transition:

		r = 1.00f;
		g = 0.65f;
		b = 0.15f;

		// Stronger than the normal passive frame.
		frameAlpha = 0.95f;

		break;


	// -----------------------------------------------------
	// Setup complete / camera handoff
	// -----------------------------------------------------
	case WorkspaceStatusTone::Ready:

		r = 0.45f;
		g = 0.95f;
		b = 1.00f;

		frameAlpha = 0.85f;

		break;


	// -----------------------------------------------------
	// Normal VitruGen workspace frame.
	// -----------------------------------------------------
	case WorkspaceStatusTone::Neutral:
	default:

		// Preserve current appearance exactly.
		r = 0.85f;
		g = 0.95f;
		b = 1.00f;

		frameAlpha = alpha;

		break;
	}


	// =========================================================
	// BLINK
	//
	// Same 0.50 second rhythm as the AUTO status line.
	// =========================================================
	if (blink) {

		using Clock = chrono::steady_clock;

		const float seconds =
			chrono::duration<float>(Clock::now().time_since_epoch()).count();

		const bool bright =
			std::fmod(seconds, 0.50f) < 0.25f;

		frameAlpha *= bright
			? 1.0f
			: 0.20f;
	}

	glLineWidth(1.0f);
	glColor4f(r, g, b, frameAlpha);
	glBegin(GL_LINE_LOOP);

	glVertex2f(x0, y0);
	glVertex2f(x1, y0);

	glVertex2f(x1, y1);
	glVertex2f(x0, y1);

	glEnd();


	if (label) {

		drawText2D(
			x0 + 18.0f,
			y0 + 30.0f,
			label,
			GLUT_BITMAP_HELVETICA_18
		);
	}
}

// =============================================================================
// PANEL BACKGROUND
// =============================================================================
void ViewPort::drawPanelBackground() {

	const float x0 = panelX(m_margin);
	const float y0 = m_margin;

	const float x1 = panelX(m_panelWidth);
	const float y1 =
		static_cast<float>(m_windowHeight) -
		m_margin;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glUseProgram(0);
	glEnable(GL_BLEND);

	glBlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	// Background.
	glColor4f(
		0.02f,
		0.04f,
		0.06f,
		0.76f * m_panelSlide
	);

	glBegin(GL_QUADS);

	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);

	glEnd();

	// Border.
	glLineWidth(1.5f);
	glColor4f(
		1.0f,
		1.0f,
		1.0f,
		0.95f *
		m_panelSlide
	);

	glBegin(GL_LINE_LOOP);

	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);

	glEnd();


	glLineWidth(1.0f);
}

// =============================================================================
// GENERIC HEADER
// =============================================================================
void ViewPort::drawPresentationHeader(const WorkspacePresentation& presentation) {

	const float x = panelX(95.0f);

	glColor4f(1.0f, 1.0f, 1.0f, m_panelSlide);
	
	drawText2D(
		x, 100.0f,
		"ANAHEIM SYSTEMS DYNAMICS",
		GLUT_BITMAP_HELVETICA_18
	);

	drawText2D(
		x, 128.0f,
		"VitruGen SIMCAD Ver 0.0.4",
		GLUT_BITMAP_HELVETICA_18
	);

	float y = 180.0f;

	if (!presentation.workspaceName.empty()) {

		drawText2D(
			x, y,
			presentation.workspaceName.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);
	}

	if (!presentation.layerLabel.empty()) {

		y += 35.0f;
		glColor4f(0.72f, 0.78f, 0.82f, m_panelSlide);

		drawText2D(
			x, y,
			presentation.layerLabel.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);
	}

	if (!presentation.subLayerLabel.empty()) {

		y += 28.0f;

		drawText2D(
			x, y,
			presentation
			.subLayerLabel
			.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);
	}

}

// =============================================================================
// GENERIC ROW
// =============================================================================
void ViewPort::drawPanelRow(
	float x,
	float y,
	const WorkspacePanelRow& row) {

	float drawX = panelX(x);

	if (row.subordinate) {

		drawX += 24.0f;
	}

	if (row.selected) {

		glColor4f(0.45f, 1.0f, 0.65f, m_panelSlide);

		drawText2D(
			drawX - 28.0f,
			y,
			">",
			GLUT_BITMAP_HELVETICA_18
		);
	}
	else if (row.emphasized) {

		glColor4f(0.85f, 0.95f, 1.0f, m_panelSlide);
	}
	else {

		glColor4f(0.72f, 0.78f, 0.82f, m_panelSlide);
	}

	string line = row.label;
	
	if (!row.value.empty()) {

		line += " { ";
		line += row.value;
		line += " }";
	}

	drawText2D(
		drawX,
		y,
		line.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);
}

// =============================================================================
// DIVIDER
// =============================================================================
void ViewPort::drawSectionDivider(float y) {

	const float x0 =
		panelX(m_margin + 42.0f);

	const float x1 =
		panelX(m_panelWidth - 42.0f);

	glColor4f(
		0.85f,
		0.95f,
		1.0f,
		0.45f *
		m_panelSlide
	);

	glBegin(GL_LINES);

	glVertex2f(x0, y);
	glVertex2f(x1, y);

	glEnd();
}

// =============================================================================
// GENERIC SECTIONS
// =============================================================================
void ViewPort::drawPresentationSections(const WorkspacePresentation& presentation) {

	float y = 280.0f;
	const float headingX = 95.0f;
	const float rowX = 95.0f;
	bool contentDrawn = false;

	for (const WorkspacePanelSection& section : presentation.sections) {

		if (!section.heading.empty()) {

			if (contentDrawn)
				y += 18.0f;

			drawSectionDivider(y);

			y += m_sectionSpacing;
			glColor4f(0.85f, 0.95f, 1.0f, m_panelSlide);

			drawText2D(
				panelX(headingX),
				y,
				section.heading.c_str(),
				GLUT_BITMAP_HELVETICA_18
			);

			y += m_rowSpacing;
			contentDrawn = true;
		}

		for (const WorkspacePanelRow& row : section.rows) {

			drawPanelRow(rowX, y, row);
			y += m_rowSpacing;
			contentDrawn = true;
		}
	}

	if (!presentation.statusLine.empty()) {

		if (contentDrawn) y += 10.0f;
		float statusAlpha = m_panelSlide;

		// ---------------------------------------------------------
		// Optional status-line blink.
		//
		// 0.50 second cycle:
		//     0.25 bright
		//     0.25 dim
		// ---------------------------------------------------------
		if (presentation.statusBlink) {

			using Clock =
				chrono::steady_clock;

			const float seconds =
				chrono::duration<float>(Clock::now().time_since_epoch()).count();

			const bool bright = fmod(seconds, 0.50f) < 0.25f;

			statusAlpha *= bright
				? 1.0f
				: 0.20f;
		}

		switch (presentation.statusTone) {
		case WorkspaceStatusTone::Ready:
			glColor4f(0.45f, 1.00f, 0.65f, m_panelSlide);
			break;

		case WorkspaceStatusTone::Caution:
			glColor4f(1.00f, 0.65f, 0.15f, m_panelSlide);
			break;

		case WorkspaceStatusTone::Warning:
			glColor4f(1.00f, 0.45f, 0.45f, m_panelSlide);
			break;

		case WorkspaceStatusTone::Transition:
			glColor4f(1.0f, 0.65f, 0.15f, statusAlpha);
			break;

		case WorkspaceStatusTone::Neutral:
		default:
			glColor4f(0.72f, 0.78f, 0.82f, m_panelSlide);
			break;
		}

		const string& status = presentation.statusLine;

		size_t lineStart = 0;
		float statusY = y;

		while (lineStart <= status.size()) {

			const size_t lineEnd =
				status.find('\n', lineStart);

			const string line =
				status.substr(
					lineStart,
					lineEnd == string::npos
					? string::npos
					: lineEnd - lineStart
				);

			drawText2D(
				panelX(rowX),
				statusY,
				line.c_str(),
				GLUT_BITMAP_HELVETICA_18
			);

			if (lineEnd == std::string::npos)
				break;

			lineStart = lineEnd + 1;

			statusY += m_rowSpacing;
		}
	}
}

void ViewPort::drawRuntimeStatusPanel(const WorkspaceRuntimeStatus& status) {
	if (!status.visible) return;

	// ---------------------------------------------------------
	// GOLD Ver004 runtime-status geometry.
	// ---------------------------------------------------------
	const float panelLeft = 24.0f;
	const float panelTop = 24.0f;
	const float panelBottom = 184.0f;

	const float viewportRight =
		(std::max)(panelLeft + 640.0f,
			static_cast<float>(m_windowWidth) - 24.0f);

	const float normalPanelRight =
		(std::min)(1120.0f, viewportRight);

	const float panelRight =
		status.auxiliaryVisible
		? viewportRight
		: normalPanelRight;

	// ---------------------------------------------------------
	// Optional right-side detail column.
	// ---------------------------------------------------------
	const float availableWidth = panelRight - 760.0f;

	const float auxiliaryWidth =
		(std::min)(520.0f, 
			(std::max)(360.0f, availableWidth));

	const float auxiliaryDividerX = panelRight - auxiliaryWidth;
	const float auxiliaryTextX = auxiliaryDividerX + 24.0f;

	// ---------------------------------------------------------
	// Presentation state.
	// ---------------------------------------------------------
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glUseProgram(0);

	glEnable(GL_BLEND);

	glBlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	// ---------------------------------------------------------
	// Main background.
	// ---------------------------------------------------------
	glColor4f(0.02f, 0.04f, 0.06f, 0.55f);

	glBegin(GL_QUADS);

	glVertex2f(panelLeft, panelTop);
	glVertex2f(panelRight, panelTop);
	glVertex2f(panelRight, panelBottom);
	glVertex2f(panelLeft, panelBottom);

	glEnd();

	// ---------------------------------------------------------
	// Auxiliary background.
	// ---------------------------------------------------------
	if (status.auxiliaryVisible) {

		glColor4f(0.01f, 0.02f, 0.04f, 0.30f);
		glBegin(GL_QUADS);

		glVertex2f(
			auxiliaryDividerX,
			panelTop
		);

		glVertex2f(
			panelRight,
			panelTop
		);

		glVertex2f(
			panelRight,
			panelBottom
		);

		glVertex2f(
			auxiliaryDividerX,
			panelBottom
		);

		glEnd();

		glLineWidth(1.0f);
		glColor4f(0.55f, 0.72f, 0.82f, 0.52f);

		glBegin(GL_LINES);

		glVertex2f(
			auxiliaryDividerX,
			panelTop + 16.0f
		);

		glVertex2f(
			auxiliaryDividerX,
			panelBottom - 16.0f
		);

		glEnd();
	}

	// ---------------------------------------------------------
	// Main runtime column.
	// ---------------------------------------------------------
	glColor3f(0.85f, 0.95f, 1.0f);

	drawText2D(
		40.0f,
		52.0f,
		status.titleLine.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawText2D(
		40.0f,
		82.0f,
		status.contextLine.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawText2D(
		40.0f,
		108.0f,
		status.objectLine.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawText2D(
		40.0f,
		134.0f,
		status.helpLine.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	// ---------------------------------------------------------
	// Optional auxiliary column.
	// ---------------------------------------------------------
	if (status.auxiliaryVisible) {

		switch (status.auxiliaryStatusTone) {

		case WorkspaceStatusTone::Ready:
			glColor3f(0.28f, 1.00f, 0.42f);
			break;

		case WorkspaceStatusTone::Caution:
			glColor3f(1.00f, 0.55f, 0.08f);
			break;

		default:
			glColor3f(0.56f, 0.64f, 0.68f);
			break;
		}

		drawText2D(
			auxiliaryTextX,
			64.0f,
			status
			.auxiliaryStatusLine
			.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		glColor3f(0.85f, 0.95f, 1.0f);

		drawText2D(
			auxiliaryTextX,
			90.0f,
			status
			.auxiliaryReferenceLine
			.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		drawText2D(
			auxiliaryTextX,
			116.0f,
			status
			.auxiliaryTargetLine
			.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);
	}
}

// =============================================================================
// FOOTER
// =============================================================================
void ViewPort::drawPresentationFooter(const WorkspacePresentation& presentation) {

	const float x = panelX(95.0f);

	const float y =
		static_cast<float>(m_windowHeight) -
		135.0f;

	glColor4f(0.75f, 0.75f, 0.75f, m_panelSlide);
	
	if (!presentation.footerLine1.empty()) {

		drawText2D(
			x, y,
			presentation
			.footerLine1
			.c_str(),
			GLUT_BITMAP_HELVETICA_12
		);
	}

	if (!presentation.footerLine2.empty()) {

		drawText2D(
			x, y + 34.0f,
			presentation
			.footerLine2
			.c_str(),
			GLUT_BITMAP_HELVETICA_12
		);
	}
}

// =============================================================================
// MAIN GENERIC OVERLAY
// =============================================================================
void ViewPort::drawOverlay(
	const WorkspacePresentation& presentation,
	const ObjExportPanelData* modalData) {

	const bool subLayerPanel = presentation.panelLayout == 
		WorkspacePanelLayout::SubLayer;

	updatePanelAnimation(presentation.panelVisible && !subLayerPanel);
	updateSubLayerPanelAnimation(presentation.panelVisible && subLayerPanel);

	beginOverlay2D();
	drawWorkspaceFrame(
		0.30f,
		presentation.frameTone,
		presentation.frameBlink,
		nullptr
	);

	// ---------------------------------------------------------
	// Layer-3 runtime HUD.
	//
	// GOLD draws this BEFORE the Sub-Layer control panel.
	// Therefore, when TAB opens the Sub-Layer panel, the panel
	// physically overlays the lower-left portion of the HUD.
	// ---------------------------------------------------------
	drawRuntimeStatusPanel(presentation.runtimeStatus);

	if (subLayerPanel) {

		if (m_subLayerPanelSlide > 0.0f)
			drawSubLayerPresentation(presentation);
	}
	else {

		if (m_panelSlide > 0.0f) {

			drawPanelBackground();
			drawPresentationHeader(presentation);
			drawPresentationSections(presentation);
			drawPresentationFooter(presentation);
		}
	}

	drawSelectionCursor(presentation.selectionCursor);

	// ---------------------------------------------------------
	// Host modal must render LAST.
	//
	// The active workspace, runtime HUD, and Sub-Layer panel
	// remain alive underneath it exactly like GOLD.
	// ---------------------------------------------------------
	if (modalData && modalData->mode != ObjExportPanelMode::HIDDEN)
		drawObjExportPanel(*modalData);

	endOverlay2D();
}

void ViewPort::drawSelectionCursor(
	const WorkspaceSelectionCursorPresentation& cursor) {
	if (!cursor.visible) return;

	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(2.0f);
	glColor4f(1.0f, 1.0f, 0.15f, 0.95f);

	const float x = static_cast<float>(cursor.x);
	const float y = static_cast<float>(cursor.y);
	const float halfSize = 10.0f;
	glBegin(GL_LINES);
	glVertex2f(x - halfSize, y);
	glVertex2f(x + halfSize, y);
	glVertex2f(x, y - halfSize);
	glVertex2f(x, y + halfSize);
	glEnd();

	glLineWidth(1.0f);
}

void ViewPort::updateSubLayerPanelAnimation(bool visible) {

	const float target = visible
		? 1.0f
		: 0.0f;

	m_subLayerPanelSlide +=
		(target - m_subLayerPanelSlide) * 0.18f;

	if (m_subLayerPanelSlide < 0.001f)
		m_subLayerPanelSlide = 0.0f;

	if (m_subLayerPanelSlide > 0.999f)
		m_subLayerPanelSlide = 1.0f;
}

void ViewPort::drawSubLayerPresentation(const WorkspacePresentation& presentation) {
	// =========================================================
	// PRESENTATION SHAPE VALIDATION
	// =========================================================
	const float alpha = m_subLayerPanelSlide;

	// ---------------------------------------------------------
	// GOLD Ver004 Sub-Layer geometry.
	// ---------------------------------------------------------
	const float panelWidth = 650.0f;
	const float x0 = m_margin;
	const float y0Visible = 170.0f;

	const float panelHeight =
		static_cast<float>(m_windowHeight) -
		y0Visible - m_margin;

	const float hiddenOffsetY = panelHeight +
		m_margin + 24.0f;

	const float y0 = y0Visible +
		hiddenOffsetY * (1.0f - alpha);

	const float x1 = x0 + panelWidth;
	const float y1 = y0 + panelHeight;

	const float sectionX = x0 + 54.0f;
	const float labelX = x0 + 84.0f;

	const float dividerX0 = x0 + 42.0f;
	const float dividerX1 = x1 - 42.0f;

	// ---------------------------------------------------------
	// Background.
	// ---------------------------------------------------------
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glUseProgram(0);

	glEnable(GL_BLEND);

	glBlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	glColor4f(0.02f, 0.04f, 0.06f, 0.80f * alpha);
	glBegin(GL_QUADS);

	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);

	glEnd();

	// ---------------------------------------------------------
	// Border.
	// ---------------------------------------------------------
	glLineWidth(1.5f);
	glColor4f(1.0f, 1.0f, 1.0f,0.95f * alpha);

	glBegin(GL_LINE_LOOP);

	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);

	glEnd();

	glColor4f(0.85f, 0.95f, 1.0f, alpha);

	drawText2D(
		sectionX,
		y0 + 40.0f,
		presentation.workspaceName.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawText2D(
		sectionX,
		y0 + 70.0f,
		presentation.subLayerLabel.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	if (presentation.nodeTrack.visible) {
		// ---------------------------------------------------------
		// GOLD Ver004 Sub-Layer-2 assembly node track.
		// ---------------------------------------------------------
		glColor4f(0.72f, 0.78f, 0.82f, alpha);

		drawText2D(
			sectionX,
			y0 + 100.0f,
			presentation.nodeTrack.activeNodeLabel.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		const float starX[4] = {
			x0 + 112.0f,
			x0 + 252.0f,
			x0 + 392.0f,
			x0 + 532.0f
		};

		// ---------------------------------------------------------
		// Assembly rail.
		// ---------------------------------------------------------
		glColor4f(0.72f, 0.78f, 0.82f, 0.72f * alpha);
		glBegin(GL_LINES);

		glVertex2f(
			starX[0] + 8.0f,
			y0 + 137.0f
		);

		glVertex2f(
			starX[3] + 8.0f,
			y0 + 137.0f
		);

		glEnd();

		// ---------------------------------------------------------
		// Node markers.
		// ---------------------------------------------------------
		for (int i = 0; i < presentation.nodeTrack.nodeCount; i++) {

			const bool active = presentation.nodeTrack.activeNode == i;

			if (active) glColor4f(0.45f, 1.0f, 0.65f, alpha);
			else glColor4f(0.55f, 0.62f, 0.68f, alpha);

			// Node marker.
			drawText2D(
				starX[i],
				y0 + 142.0f,
				"*",
				GLUT_BITMAP_HELVETICA_18
			);

			char nodeLabel[32];

			snprintf(
				nodeLabel,
				sizeof(nodeLabel),
				"Node_%d",
				i
			);

			// Node name inherits same active/inactive color.
			drawText2D(
				starX[i] - 18.0f,
				y0 + 168.0f,
				nodeLabel,
				GLUT_BITMAP_HELVETICA_12
			);
		}
	}

	auto drawDivider = [&](float y) {

		glColor4f(0.85f, 0.95f, 1.0f, 0.45f * alpha);
		glBegin(GL_LINES);

		glVertex2f(dividerX0, y);
		glVertex2f(dividerX1, y);

		glEnd();
	};

	auto drawSubRow = [&](float y, const WorkspacePanelRow& row) {
		
		if (row.selected) {

			glColor4f(0.45f, 1.0f, 0.65f, alpha);

			drawText2D(
				labelX - 22.0f,
				y,
				">",
				GLUT_BITMAP_HELVETICA_18
			);
		}
		else {
			switch (row.tone) {
			case WorkspaceStatusTone::Ready:
				glColor4f(0.45f, 1.0f, 0.65f, alpha);
				break;
			case WorkspaceStatusTone::Caution:
				glColor4f(0.95f, 0.82f, 0.30f, alpha);
				break;
			case WorkspaceStatusTone::Warning:
				glColor4f(1.0f, 0.38f, 0.08f, alpha);
				break;
			default:
				glColor4f(0.72f, 0.78f, 0.82f, alpha);
				break;
			}
		}

		string line = row.label;

		if (!row.value.empty()) {

			line += " { ";
			line += row.value;
			line += " }";
		}

		drawText2D(
			labelX,
			y,
			line.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);
	};

	auto drawExplicitTitle = [&](float y,
		const WorkspacePanelSection& section) {
		glColor4f(0.85f, 0.95f, 1.0f, alpha);
		drawText2D(sectionX, y, section.heading.c_str(),
			GLUT_BITMAP_HELVETICA_18);
	};

	auto drawNote = [&](float y, const string& note) {
		glColor4f(0.72f, 0.78f, 0.82f, alpha);
		drawText2D(sectionX, y, note.c_str(),
			GLUT_BITMAP_HELVETICA_12);
	};

	auto drawInfoRow = [&](float y, const WorkspacePanelRow& row) {
		switch (row.tone) {
		case WorkspaceStatusTone::Ready:
			glColor4f(0.45f, 1.0f, 0.65f, alpha);
			break;
		case WorkspaceStatusTone::Caution:
			glColor4f(0.95f, 0.82f, 0.30f, alpha);
			break;
		case WorkspaceStatusTone::Warning:
			glColor4f(1.0f, 0.50f, 0.25f, alpha);
			break;
		default:
			glColor4f(0.72f, 0.78f, 0.82f, alpha);
			break;
		}
		string line = row.label;
		if (!row.value.empty()) {
			line += " ";
			line += row.value;
		}
		drawText2D(sectionX, y, line.c_str(),
			GLUT_BITMAP_HELVETICA_12);
	};

	auto drawGoldFooter = [&]() {
		drawDivider(y1 - 92.0f);
		glColor4f(0.75f, 0.75f, 0.75f, alpha);
		drawText2D(sectionX, y1 - 58.0f,
			"W/S: Select    A/D: Change value    E: Activate    TAB: Hide    Q: Back",
			GLUT_BITMAP_HELVETICA_12);
		glLineWidth(1.0f);
	};

	using SubLayout = WorkspaceSubLayerPanelLayout;
	const SubLayout explicitLayout = presentation.subLayerPanelLayout;

	if (explicitLayout == SubLayout::TextureMapCompact) {
		drawDivider(y0 + 105.0f);
		float y = y0 + 145.0f;
		const float infoX = labelX + 28.0f;
		for (const WorkspacePanelSection& section :
			presentation.sections) {
			if (y >= y1 - 132.0f) break;
			glColor4f(0.85f, 0.95f, 1.0f, alpha);
			drawText2D(sectionX, y, section.heading.c_str(),
				GLUT_BITMAP_HELVETICA_18);
			y += 40.0f;
			for (const WorkspacePanelRow& row : section.rows) {
				if (y >= y1 - 126.0f) break;
				const float x = row.subordinate ? infoX : labelX;
				if (row.selectable) {
					if (row.selected) {
						glColor4f(0.45f, 1.0f, 0.65f, alpha);
						drawText2D(x - 22.0f, y, ">",
							GLUT_BITMAP_HELVETICA_18);
					}
					else glColor4f(0.72f, 0.78f, 0.82f, alpha);
				}
				else if (row.emphasized ||
					row.tone == WorkspaceStatusTone::Ready)
					glColor4f(0.45f, 1.0f, 0.65f, alpha);
				else glColor4f(0.72f, 0.78f, 0.82f, alpha);
				string line = row.label;
				if (!row.value.empty()) {
					line += " { ";
					line += row.value;
					line += " }";
				}
				drawText2D(x, y, line.c_str(),
					row.subordinate
					? GLUT_BITMAP_HELVETICA_12
					: GLUT_BITMAP_HELVETICA_18);
				y += row.subordinate ? 24.0f : 34.0f;
			}
			drawDivider(y + 4.0f);
			y += 28.0f;
		}
		drawDivider(y1 - 92.0f);
		glColor4f(0.75f, 0.75f, 0.75f, alpha);
		drawText2D(sectionX, y1 - 58.0f,
			presentation.footerLine1.c_str(),
			GLUT_BITMAP_HELVETICA_12);
		if (!presentation.footerLine2.empty()) {
			glColor4f(0.85f, 0.95f, 1.0f, alpha);
			drawText2D(sectionX, y1 - 34.0f,
				presentation.footerLine2.c_str(),
				GLUT_BITMAP_HELVETICA_12);
		}
		glLineWidth(1.0f);
		return;
	}

	// GOLD Sub-Layer 2/3 coordinates. The workspace selects a
	// semantic layout; ViewPort remains responsible for geometry.
	if (explicitLayout == SubLayout::MarchingCubesReport &&
		presentation.sections.size() >= 4) {
		if (!presentation.panelContextLine.empty()) {
			glColor4f(0.72f, 0.78f, 0.82f, alpha);
			drawText2D(sectionX, y0 + 108.0f,
				presentation.panelContextLine.c_str(),
				GLUT_BITMAP_HELVETICA_18);
		}
		drawExplicitTitle(y0 + 140.0f, presentation.sections[0]);
		const auto& reportRows = presentation.sections[0].rows;
		for (size_t i = 0; i < reportRows.size() && i < 4; ++i)
			drawInfoRow(y0 + 166.0f + 22.0f * static_cast<float>(i),
				reportRows[i]);
		drawDivider(y0 + 254.0f);

		drawExplicitTitle(y0 + 292.0f, presentation.sections[1]);
		if (presentation.sections[1].rows.size() >= 3) {
			drawSubRow(y0 + 334.0f, presentation.sections[1].rows[0]);
			drawSubRow(y0 + 376.0f, presentation.sections[1].rows[1]);
			drawSubRow(y0 + 418.0f, presentation.sections[1].rows[2]);
		}
		drawDivider(y0 + 454.0f);

		drawExplicitTitle(y0 + 494.0f, presentation.sections[2]);
		if (!presentation.sections[2].rows.empty())
			drawSubRow(y0 + 536.0f, presentation.sections[2].rows[0]);
		drawDivider(y0 + 572.0f);

		drawExplicitTitle(y0 + 612.0f, presentation.sections[3]);
		if (!presentation.sections[3].rows.empty())
			drawSubRow(y0 + 654.0f, presentation.sections[3].rows[0]);
		drawGoldFooter();
		return;
	}

	if (explicitLayout != SubLayout::Automatic &&
		explicitLayout != SubLayout::AssemblyPreview) {
		drawDivider(y0 + 190.0f);

		if (explicitLayout == SubLayout::AssemblyEditVolume0 &&
			presentation.sections.size() >= 4) {
			drawExplicitTitle(y0 + 224.0f, presentation.sections[0]);
			drawExplicitTitle(y0 + 258.0f, presentation.sections[1]);
			if (!presentation.sections[1].rows.empty())
				drawSubRow(y0 + 298.0f, presentation.sections[1].rows[0]);
			drawDivider(y0 + 328.0f);
			drawExplicitTitle(y0 + 364.0f, presentation.sections[2]);
			if (!presentation.sections[2].rows.empty())
				drawSubRow(y0 + 404.0f, presentation.sections[2].rows[0]);
			drawDivider(y0 + 434.0f);
			drawExplicitTitle(y0 + 470.0f, presentation.sections[3]);
			if (presentation.sections[3].rows.size() >= 2) {
				drawSubRow(y0 + 510.0f, presentation.sections[3].rows[0]);
				drawSubRow(y0 + 550.0f, presentation.sections[3].rows[1]);
			}
			if (!presentation.sections[3].notes.empty())
				drawNote(y0 + 636.0f, presentation.sections[3].notes[0]);
		}
		else if ((explicitLayout == SubLayout::AssemblyEditTargetVolume0 ||
			explicitLayout == SubLayout::AssemblyEditTargetVolume1) &&
			presentation.sections.size() >= 3) {
			drawExplicitTitle(y0 + 224.0f, presentation.sections[0]);
			if (presentation.sections[0].rows.size() >= 2) {
				drawSubRow(y0 + 266.0f, presentation.sections[0].rows[0]);
				drawSubRow(y0 + 306.0f, presentation.sections[0].rows[1]);
			}
			drawDivider(y0 + 342.0f);
			drawExplicitTitle(y0 + 382.0f, presentation.sections[1]);
			if (!presentation.sections[1].rows.empty())
				drawSubRow(y0 + 424.0f, presentation.sections[1].rows[0]);
			drawDivider(y0 + 460.0f);
			drawExplicitTitle(y0 + 500.0f, presentation.sections[2]);
			if (explicitLayout == SubLayout::AssemblyEditTargetVolume0 &&
				presentation.sections[2].rows.size() >= 2) {
				drawSubRow(y0 + 542.0f, presentation.sections[2].rows[0]);
				drawSubRow(y0 + 584.0f, presentation.sections[2].rows[1]);
				if (!presentation.sections[2].notes.empty())
					drawNote(y0 + 678.0f, presentation.sections[2].notes[0]);
			}
			else {
				if (!presentation.sections[2].rows.empty())
					drawSubRow(y0 + 542.0f, presentation.sections[2].rows[0]);
				if (!presentation.sections[2].notes.empty())
					drawNote(y0 + 604.0f, presentation.sections[2].notes[0]);
				if (presentation.sections[2].notes.size() >= 2)
					drawNote(y0 + 626.0f, presentation.sections[2].notes[1]);
			}
		}
		else if (explicitLayout == SubLayout::AssemblyOffsetVolume0 &&
			presentation.sections.size() >= 2) {
			drawExplicitTitle(y0 + 230.0f, presentation.sections[0]);
			if (presentation.sections[0].rows.size() >= 2) {
				drawSubRow(y0 + 272.0f, presentation.sections[0].rows[0]);
				drawSubRow(y0 + 314.0f, presentation.sections[0].rows[1]);
			}
			drawDivider(y0 + 350.0f);
			drawExplicitTitle(y0 + 390.0f, presentation.sections[1]);
			if (presentation.sections[1].rows.size() >= 2) {
				drawSubRow(y0 + 432.0f, presentation.sections[1].rows[0]);
				drawSubRow(y0 + 480.0f, presentation.sections[1].rows[1]);
			}
		}
		else if ((explicitLayout == SubLayout::AssemblyOffsetTargetVolume0 ||
			explicitLayout == SubLayout::AssemblyOffsetTargetVolume1) &&
			presentation.sections.size() >= 3) {
			drawExplicitTitle(y0 + 230.0f, presentation.sections[0]);
			if (!presentation.sections[0].rows.empty())
				drawSubRow(y0 + 272.0f, presentation.sections[0].rows[0]);
			drawDivider(y0 + 310.0f);
			drawExplicitTitle(y0 + 350.0f, presentation.sections[1]);
			if (presentation.sections[1].rows.size() >= 2) {
				drawSubRow(y0 + 392.0f, presentation.sections[1].rows[0]);
				drawSubRow(y0 + 434.0f, presentation.sections[1].rows[1]);
			}
			drawDivider(y0 + 472.0f);
			drawExplicitTitle(y0 + 512.0f, presentation.sections[2]);
			if (!presentation.sections[2].rows.empty())
				drawSubRow(y0 + 554.0f, presentation.sections[2].rows[0]);
			if (explicitLayout == SubLayout::AssemblyOffsetTargetVolume0 &&
				presentation.sections.size() >= 4) {
				drawDivider(y0 + 592.0f);
				drawExplicitTitle(y0 + 632.0f, presentation.sections[3]);
				if (presentation.sections[3].rows.size() >= 2) {
					drawSubRow(y0 + 674.0f, presentation.sections[3].rows[0]);
					drawSubRow(y0 + 716.0f, presentation.sections[3].rows[1]);
				}
			}
			else {
				if (!presentation.sections[2].notes.empty())
					drawNote(y0 + 620.0f, presentation.sections[2].notes[0]);
				if (presentation.sections[2].notes.size() >= 2)
					drawNote(y0 + 642.0f, presentation.sections[2].notes[1]);
			}
		}
		else if (explicitLayout == SubLayout::AssemblyApply &&
			presentation.sections.size() >= 2) {
			drawExplicitTitle(y0 + 230.0f, presentation.sections[0]);
			if (!presentation.sections[0].rows.empty())
				drawSubRow(y0 + 282.0f, presentation.sections[0].rows[0]);
			drawDivider(y0 + 326.0f);
			drawExplicitTitle(y0 + 366.0f, presentation.sections[1]);
			if (presentation.sections[1].rows.size() >= 2) {
				drawSubRow(y0 + 408.0f, presentation.sections[1].rows[0]);
				drawSubRow(y0 + 450.0f, presentation.sections[1].rows[1]);
			}
		}

		drawGoldFooter();
		return;
	}

	const bool renderingSetupLayout =
		presentation.sections.size() >= 4 &&
		presentation.sections[0].rows.size() >= 1 &&
		presentation.sections[1].rows.size() >= 1 &&
		presentation.sections[2].rows.size() >= 2 &&
		presentation.sections[3].rows.size() >= 2;

	const bool collisionSetupLayout =
		presentation.sections.size() >= 4 &&
		presentation.sections[0].rows.size() >= 1 &&
		presentation.sections[1].rows.size() >= 1 &&
		presentation.sections[2].rows.size() >= 2 &&
		presentation.sections[3].rows.size() >= 1;

	const bool volumePreviewLayout =
		presentation.nodeTrack.visible &&
		presentation.sections.size() == 3 &&
		presentation.sections[0].rows.size() >= 1 &&
		presentation.sections[1].rows.size() >= 1 &&
		presentation.sections[2].rows.size() >= 2;

	// =========================================================
	// GOLD SUB-LAYER 1 — RENDERING SETUP
	// =========================================================
	if (renderingSetupLayout) {

		auto drawSectionTitle =
			[&](float y, const WorkspacePanelSection& section) {

			glColor4f(0.85f, 0.95f, 1.0f, alpha);

			drawText2D(
				sectionX,
				y,
				section.heading.c_str(),
				GLUT_BITMAP_HELVETICA_18
			);
		};

		drawDivider(y0 + 105.0f);

		// =====================================================
		// Render Source
		// =====================================================
		drawSectionTitle(
			y0 + 145.0f,
			presentation.sections[0]
		);

		drawSubRow(
			y0 + 185.0f,
			presentation.sections[0].rows[0]
		);

		drawDivider(y0 + 220.0f);

		// =====================================================
		// Mesh Scale Bound
		// =====================================================
		drawSectionTitle(
			y0 + 260.0f,
			presentation.sections[1]
		);

		drawSubRow(
			y0 + 300.0f,
			presentation.sections[1].rows[0]
		);

		drawDivider(y0 + 335.0f);

		// =====================================================
		// Debug Presentation
		// =====================================================
		drawSectionTitle(
			y0 + 375.0f,
			presentation.sections[2]
		);

		drawSubRow(
			y0 + 415.0f,
			presentation.sections[2].rows[0]
		);

		drawSubRow(
			y0 + 457.0f,
			presentation.sections[2].rows[1]
		);

		drawDivider(y0 + 492.0f);

		// =====================================================
		// Next / Previous Sub-Layer
		// =====================================================
		drawSectionTitle(
			y0 + 532.0f,
			presentation.sections[3]
		);

		drawSubRow(
			y0 + 574.0f,
			presentation.sections[3].rows[0]
		);

		drawSubRow(
			y0 + 616.0f,
			presentation.sections[3].rows[1]
		);

		// =====================================================
		// Footer
		// =====================================================
		drawDivider(y1 - 92.0f);
		glColor4f(0.75f, 0.75f, 0.75f, alpha);

		drawText2D(
			sectionX,
			y1 - 58.0f,
			"W/S: Select    A/D: Change value    "
			"E: Activate    TAB: Hide    Q: Back",
			GLUT_BITMAP_HELVETICA_12
		);

		glLineWidth(1.0f);
		return;
	}

	if (volumePreviewLayout) {
		// =========================================================
		// NODE DESCRIPTION
		// =========================================================
		glColor4f(0.72f, 0.78f, 0.82f, alpha);

		drawText2D(
			sectionX,
			y0 + 100.0f,
			presentation.nodeTrack.activeNodeLabel.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		// =========================================================
		// GOLD ASSEMBLY NODE TRACK
		// =========================================================
		const float starX[4] = {
			x0 + 112.0f,
			x0 + 252.0f,
			x0 + 392.0f,
			x0 + 532.0f
		};

		glColor4f(0.72f, 0.78f, 0.82f, 0.72f * alpha);

		glBegin(GL_LINES);

		glVertex2f(
			starX[0] + 8.0f,
			y0 + 137.0f
		);

		glVertex2f(
			starX[3] + 8.0f,
			y0 + 137.0f
		);

		glEnd();


		for (int i = 0; i < 4; i++) {

			const bool active =
				presentation.nodeTrack.activeNode == i;

			if (active) glColor4f(0.45f, 1.0f, 0.65f, alpha);
			else glColor4f(0.55f, 0.62f, 0.68f, alpha);

			drawText2D(
				starX[i],
				y0 + 142.0f,
				"*",
				GLUT_BITMAP_HELVETICA_18
			);

			char nodeLabel[32];

			snprintf(
				nodeLabel,
				sizeof(nodeLabel),
				"Node_%d",
				i
			);

			drawText2D(
				starX[i] - 18.0f,
				y0 + 168.0f,
				nodeLabel,
				GLUT_BITMAP_HELVETICA_12
			);
		}

		// =========================================================
		// Divider after assembly track
		// =========================================================
		drawDivider(y0 + 195.0f);

		// =========================================================
		// Volume Injection
		// =========================================================
		glColor4f(0.85f, 0.95f, 1.0f, alpha);

		drawText2D(
			sectionX,
			y0 + 235.0f,
			presentation.sections[0].heading.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		drawSubRow(
			y0 + 277.0f,
			presentation.sections[0].rows[0]
		);

		drawDivider(y0 + 317.0f);

		// =========================================================
		// Next Node
		// =========================================================
		glColor4f(0.85f, 0.95f, 1.0f, alpha);

		drawText2D(
			sectionX,
			y0 + 357.0f,
			presentation.sections[1].heading.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		drawSubRow(
			y0 + 399.0f,
			presentation.sections[1].rows[0]
		);

		drawDivider(y0 + 439.0f);


		// =========================================================
		// Next / Previous Sub-Layer
		// =========================================================
		glColor4f(0.85f, 0.95f, 1.0f, alpha);

		drawText2D(
			sectionX,
			y0 + 479.0f,
			presentation.sections[2].heading.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		drawSubRow(
			y0 + 521.0f,
			presentation.sections[2].rows[0]
		);

		drawSubRow(
			y0 + 563.0f,
			presentation.sections[2].rows[1]
		);


		// =========================================================
		// Footer
		// =========================================================
		drawDivider(y1 - 92.0f);
		glColor4f(0.75f, 0.75f, 0.75f, alpha);

		drawText2D(
			sectionX,
			y1 - 58.0f,
			"W/S: Select    A/D: Change value    "
			"E: Activate    TAB: Hide    Q: Back",
			GLUT_BITMAP_HELVETICA_12
		);

		glLineWidth(1.0f);

		return;
	}

	// =========================================================
	// GOLD SUB-LAYER 2 — TEMP SAFE RENDER
	// =========================================================
	if (!collisionSetupLayout) {

		float y = y0 + 145.0f;

		drawDivider(y0 + 105.0f);

		for (const WorkspacePanelSection& section :
			presentation.sections) {

			if (y >= y1 - 145.0f) {

				break;
			}

			// -------------------------------------------------
			// Section title
			// -------------------------------------------------
			glColor4f(0.85f, 0.95f, 1.0f, alpha);

			drawText2D(
				sectionX,
				y,
				section.heading.c_str(),
				GLUT_BITMAP_HELVETICA_18
			);

			y += 42.0f;

			// -------------------------------------------------
			// Rows
			// -------------------------------------------------
			for (const WorkspacePanelRow& row :
				section.rows) {

				if (y >= y1 - 145.0f) {

					break;
				}

				drawSubRow(y, row);

				y += 42.0f;
			}

			drawDivider(y + 4.0f);

			y += 40.0f;
		}


		// -----------------------------------------------------
		// Footer
		// -----------------------------------------------------
		drawDivider(y1 - 92.0f);

		glColor4f(0.75f, 0.75f, 0.75f, alpha);

		drawText2D(
			sectionX,
			y1 - 58.0f,
			"W/S: Select    A/D: Change value    "
			"E: Activate    TAB: Hide    Q: Back",
			GLUT_BITMAP_HELVETICA_12
		);

		glLineWidth(1.0f);
		return;
	}

	// =========================================================
	// GOLD SUB-LAYER 0 — COLLISION SETUP
	// =========================================================
	drawDivider(y0 + 105.0f);

	// =========================================================
	// SECTION 0 — Particle Object
	// =========================================================
	glColor4f(0.85f, 0.95f, 1.0f, alpha);

	drawText2D(
		sectionX,
		y0 + 145.0f,
		presentation.sections[0].heading.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawSubRow(
		y0 + 185.0f,
		presentation.sections[0].rows[0]
	);

	drawDivider(y0 + 225.0f);

	// =========================================================
	// SECTION 1 — Collision Proxy
	// =========================================================
	glColor4f(0.85f, 0.95f, 1.0f, alpha);

	drawText2D(
		sectionX,
		y0 + 265.0f,
		presentation.sections[1].heading.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawSubRow(
		y0 + 307.0f,
		presentation.sections[1].rows[0]
	);

	const bool sphereAvailable =
		presentation.sections[1].rows[0].value == "SPHERE";

	if (sphereAvailable) {

		glColor4f(0.45f, 1.0f, 0.65f, alpha);

		drawText2D(
			sectionX,
			y0 + 347.0f,
			"SPHERE COLLISION PROXY AVAILABLE",
			GLUT_BITMAP_HELVETICA_12
		);
	}
	else {

		glColor4f(0.72f, 0.78f, 0.82f, alpha);

		drawText2D(
			sectionX,
			y0 + 347.0f,
			"SELECTED COLLISION PROXY RESERVED",
			GLUT_BITMAP_HELVETICA_12
		);
	}

	drawDivider(y0 + 385.0f);

	// =========================================================
	// SECTION 2 — Static Particle Asset
	// =========================================================
	glColor4f(0.85f, 0.95f, 1.0f, alpha);

	drawText2D(
		sectionX,
		y0 + 425.0f,
		presentation.sections[2].heading.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawSubRow(
		y0 + 467.0f,
		presentation.sections[2].rows[0]
	);

	drawSubRow(
		y0 + 509.0f,
		presentation.sections[2].rows[1]
	);

	drawDivider(y0 + 544.0f);

	// =========================================================
	// SECTION 3 — Next Sub-Layer
	// =========================================================
	glColor4f(0.85f, 0.95f, 1.0f, alpha);

	drawText2D(
		sectionX,
		y0 + 584.0f,
		presentation.sections[3].heading.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	drawSubRow(
		y0 + 626.0f,
		presentation.sections[3].rows[0]
	);

	drawDivider(y1 - 92.0f);
	glColor4f(0.75f, 0.75f, 0.75f, alpha);

	drawText2D(
		sectionX,
		y1 - 58.0f,
		"W/S: Select    A/D: Change value    "
		"E: Activate    TAB: Hide    Q: Back",
		GLUT_BITMAP_HELVETICA_12
	);

	glLineWidth(1.0f);
}

void ViewPort::drawObjExportPanel(const ObjExportPanelData& data) {
	if (data.mode == ObjExportPanelMode::HIDDEN) return;

	const float screenW = static_cast<float>(m_windowWidth);
	const float screenH = static_cast<float>(m_windowHeight);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glUseProgram(0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// ---------------------------------------------------------
	// Full-screen modal veil.
	// ---------------------------------------------------------
	glColor4f(0.0f, 0.0f, 0.0f, 0.68f);

	glBegin(GL_QUADS);
	glVertex2f(0.0f, 0.0f);
	glVertex2f(screenW, 0.0f);
	glVertex2f(screenW, screenH);
	glVertex2f(0.0f, screenH);
	glEnd();

	const bool confirmation =
		data.mode == ObjExportPanelMode::CONFIRM;
	const bool nameEntry =
		data.mode == ObjExportPanelMode::NAME_ENTRY;
	const bool compactDialog = confirmation || nameEntry;

	const float panelW =
		compactDialog
		? 580.0f
		: min(1120.0f, screenW - 100.0f);

	const float panelH =
		compactDialog
		? 280.0f
		: min(660.0f, screenH - 100.0f);

	const float x0 = 0.5f * (screenW - panelW);
	const float y0 = 0.5f * (screenH - panelH);

	const float x1 = x0 + panelW;
	const float y1 = y0 + panelH;

	// ---------------------------------------------------------
	// Main modal body.
	// ---------------------------------------------------------
	glColor4f(0.015f, 0.028f, 0.045f, 0.97f);

	glBegin(GL_QUADS);
	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);
	glEnd();

	glLineWidth(1.5f);
	glColor4f(0.82f, 0.95f, 1.0f, 0.96f);

	glBegin(GL_LINE_LOOP);
	glVertex2f(x0, y0);
	glVertex2f(x1, y0);
	glVertex2f(x1, y1);
	glVertex2f(x0, y1);
	glEnd();

	const float textX = x0 + 38.0f;

	if (data.mode == ObjExportPanelMode::SELECT) {

		glColor4f(0.85f, 0.95f, 1.0f, 1.0f);

		drawText2D(textX, y0 + 44.0f,
			data.titleText.empty() ? "VITRUGEN STATIC PARTICLE LOAD" : data.titleText.c_str(),
			GLUT_BITMAP_HELVETICA_18);

		glColor4f(0.62f, 0.70f, 0.75f, 1.0f);

		drawText2D(textX, y0 + 76.0f,
			"Valid VSPA bundles under INPUTS and OUTPUT/STATIC_PARTICLES",
			GLUT_BITMAP_HELVETICA_12);

		const int first = max(0, data.selectedIndex - 8);
		const int last = min(static_cast<int>(data.selectionLines.size()), first + 17);
		float rowY = y0 + 118.0f;

		if (data.selectionLines.empty()) {
			glColor4f(1.0f, 0.55f, 0.25f, 1.0f);
			drawText2D(textX, rowY, "No VSPA manifests found.", GLUT_BITMAP_HELVETICA_18);
		}

		for (int i = first; i < last; i++) {

			const bool active =
				i == data.selectedIndex;

			glColor4f(active ? 0.45f : 0.72f, active ? 1.0f : 0.80f,
				active ? 0.65f : 0.84f, 1.0f);

			string row = active ? "> " : "  ";
			row += data.selectionLines[static_cast<size_t>(i)];
			drawText2D(textX, rowY, row.c_str(), GLUT_BITMAP_HELVETICA_12);
			rowY += 28.0f;
		}

		glColor4f(0.62f, 0.68f, 0.72f, 1.0f);

		drawText2D(textX, y1 - 34.0f,
			"W/S: Select    E: Load selected asset    Q: Cancel",
			GLUT_BITMAP_HELVETICA_12);

		return;
	}

	if (nameEntry) {
		glColor4f(0.85f, 0.95f, 1.0f, 1.0f);
		drawText2D(textX, y0 + 52.0f,
			data.titleText.empty() ? "VITRUGEN STATIC PARTICLE SAVE AS"
			: data.titleText.c_str(), GLUT_BITMAP_HELVETICA_18);

		glColor4f(0.72f, 0.78f, 0.82f, 1.0f);
		drawText2D(textX, y0 + 94.0f,
			data.promptText.empty() ? "STATIC PARTICLE ASSET NAME"
			: data.promptText.c_str(), GLUT_BITMAP_HELVETICA_18);

		glColor4f(0.45f, 1.0f, 0.65f, 1.0f);
		const string entry = "> NAME { " + data.inputText + "_ }";
		drawText2D(textX, y0 + 146.0f, entry.c_str(),
			GLUT_BITMAP_HELVETICA_18);

		if (!data.statusText.empty()) {
			glColor4f(0.72f, 0.78f, 0.82f, 1.0f);
			drawText2D(textX, y0 + 184.0f, data.statusText.c_str(),
				GLUT_BITMAP_HELVETICA_12);
		}

		glColor4f(0.62f, 0.68f, 0.72f, 1.0f);
		drawText2D(textX, y1 - 34.0f,
			"Type asset name    ENTER: Continue    ESC: Cancel",
			GLUT_BITMAP_HELVETICA_12);
		glLineWidth(1.0f);
		return;
	}

	// =========================================================
	// Confirmation dialog
	// =========================================================
	if (confirmation) {

		glColor4f(0.85f, 0.95f, 1.0f, 1.0f);
		drawText2D(
			textX,
			y0 + 52.0f,
			data.titleText.empty() ? "VITRUGEN OBJ EXPORT" : data.titleText.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		glColor4f(0.72f, 0.78f, 0.82f, 1.0f);
		drawText2D(
			textX,
			y0 + 94.0f,
			data.confirmText.empty() ? "Confirm Export .OBJ?" : data.confirmText.c_str(),
			GLUT_BITMAP_HELVETICA_18
		);

		auto drawChoice =
			[&](float y, bool active, const char* text) {

			if (active) {

				glColor4f(0.45f, 1.0f, 0.65f, 1.0f);
				drawText2D(
					textX,
					y,
					">",
					GLUT_BITMAP_HELVETICA_18
				);
			}
			else {

				glColor4f(0.72f, 0.78f, 0.82f, 1.0f);
			}

			drawText2D(
				textX + 28.0f,
				y,
				text,
				GLUT_BITMAP_HELVETICA_18
			);
		};

		drawChoice(
			y0 + 142.0f,
			data.yesSelected,
			"[1] Yes"
		);

		drawChoice(
			y0 + 182.0f,
			!data.yesSelected,
			"[2] No"
		);

		glColor4f(0.62f, 0.68f, 0.72f, 1.0f);
		drawText2D(
			textX,
			y1 - 34.0f,
			"W/S or A/D: Select    E: Activate    Q: Cancel",
			GLUT_BITMAP_HELVETICA_12
		);

		glLineWidth(1.0f);
		return;
	}

	// =========================================================
	// Export progress display
	// =========================================================
	glColor4f(0.85f, 0.95f, 1.0f, 1.0f);

	drawText2D(
		textX,
		y0 + 44.0f,
		data.titleText.empty() ? "VITRUGEN OBJ EXPORT PIPELINE" : data.titleText.c_str(),
		GLUT_BITMAP_HELVETICA_18
	);

	const char* phaseText =
		data.mode == ObjExportPanelMode::COMPLETE
		? "STATUS: COMPLETE"
		: data.mode == ObjExportPanelMode::FAILED
		? "STATUS: OPERATION FAILED"
		: "STATUS: PROCESSING";

	if (data.mode == ObjExportPanelMode::COMPLETE) {
		glColor4f(0.45f, 1.0f, 0.65f, 1.0f);
	}
	else if (data.mode == ObjExportPanelMode::FAILED) {
		glColor4f(1.0f, 0.35f, 0.22f, 1.0f);
	}
	else {
		glColor4f(0.95f, 0.82f, 0.30f, 1.0f);
	}

	drawText2D(
		textX,
		y0 + 74.0f,
		phaseText,
		GLUT_BITMAP_HELVETICA_12
	);

	// ---------------------------------------------------------
	// Console-style output window.
	// ---------------------------------------------------------
	const float consoleX0 = x0 + 34.0f;
	const float consoleY0 = y0 + 98.0f;
	const float consoleX1 = x1 - 34.0f;
	const float consoleY1 = y1 - 142.0f;

	glColor4f(0.005f, 0.010f, 0.016f, 0.92f);

	glBegin(GL_QUADS);
	glVertex2f(consoleX0, consoleY0);
	glVertex2f(consoleX1, consoleY0);
	glVertex2f(consoleX1, consoleY1);
	glVertex2f(consoleX0, consoleY1);
	glEnd();

	glColor4f(0.48f, 0.58f, 0.64f, 0.92f);

	glBegin(GL_LINE_LOOP);
	glVertex2f(consoleX0, consoleY0);
	glVertex2f(consoleX1, consoleY0);
	glVertex2f(consoleX1, consoleY1);
	glVertex2f(consoleX0, consoleY1);
	glEnd();

	const int maxVisibleLines = 16;

	const int lineCount =
		static_cast<int>(data.logLines.size());

	const int firstLine =
		max(0, lineCount - maxVisibleLines);

	float logY = consoleY0 + 24.0f;

	for (int i = firstLine; i < lineCount; i++) {

		glColor4f(0.72f, 0.82f, 0.86f, 1.0f);

		drawText2D(
			consoleX0 + 16.0f,
			logY,
			data.logLines[i].c_str(),
			GLUT_BITMAP_HELVETICA_12
		);

		logY += 20.0f;
	}

	// ---------------------------------------------------------
	// Progress bar.
	//
	// Twenty slash characters:
	//     one slash = five percent.
	// ---------------------------------------------------------
	const int progress =
		max(0, min(100, data.progressPercent));

	const int filledSlashes = progress / 5;

	const float progressY = y1 - 92.0f;

	glColor4f(0.82f, 0.90f, 0.94f, 1.0f);

	drawText2D(
		textX,
		progressY,
		"Loading:",
		GLUT_BITMAP_HELVETICA_18
	);

	float slashX = textX + 92.0f;

	const float slashAdvance =
		static_cast<float>(glutBitmapWidth(GLUT_BITMAP_HELVETICA_18, '/'));

	for (int i = 0; i < 20; i++) {

		if (i < filledSlashes) {
			glColor4f(0.30f, 1.0f, 0.55f, 1.0f);
		}
		else {
			glColor4f(0.80f, 0.86f, 0.90f, 0.82f);
		}

		drawText2D(
			slashX,
			progressY,
			"/",
			GLUT_BITMAP_HELVETICA_18
		);

		slashX += slashAdvance;
	}

	char percentText[32];

	snprintf(
		percentText,
		sizeof(percentText),
		" %d%%",
		progress
	);

	glColor4f(0.85f, 0.95f, 1.0f, 1.0f);

	drawText2D(
		slashX + 8.0f,
		progressY,
		percentText,
		GLUT_BITMAP_HELVETICA_18
	);

	// ---------------------------------------------------------
	// Spinner/status line.
	// ---------------------------------------------------------
	const char spinnerFrames[4] = {
		'/',
		'-',
		'\\',
		'-'
	};

	char operationLine[256];

	if (data.mode == ObjExportPanelMode::COMPLETE) {
		snprintf(
			operationLine,
			sizeof(operationLine),
			"Static asset operation ... COMPLETE!"
		);
	}
	else if (data.mode == ObjExportPanelMode::FAILED) {
		snprintf(
			operationLine,
			sizeof(operationLine),
			"Static asset operation ... FAILED"
		);
	}
	else {
		snprintf(
			operationLine,
			sizeof(operationLine),
			"Static asset operation ...%c",
			spinnerFrames[data.spinnerFrame % 4]
		);
	}

	if (data.mode == ObjExportPanelMode::FAILED) {
		glColor4f(1.0f, 0.35f, 0.22f, 1.0f);
	}
	else {
		glColor4f(0.45f, 1.0f, 0.65f, 1.0f);
	}

	drawText2D(
		textX,
		y1 - 50.0f,
		operationLine,
		GLUT_BITMAP_HELVETICA_18
	);

	if (!data.statusText.empty()) {
		glColor4f(0.68f, 0.74f, 0.78f, 1.0f);

		drawText2D(
			x1 - 360.0f,
			y1 - 50.0f,
			data.statusText.c_str(),
			GLUT_BITMAP_HELVETICA_12
		);
	}

	glLineWidth(1.0f);
}
