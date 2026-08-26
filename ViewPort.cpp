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

	glLineWidth(1.0f);
	glColor4f(0.85f, 0.95f, 1.0f, alpha);

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
			glColor4f(0.45f, 1.0f, 0.65f, m_panelSlide);
			break;

		case WorkspaceStatusTone::Warning:
			glColor4f(1.0f, 0.45f, 0.45f, m_panelSlide);
			break;

		case WorkspaceStatusTone::Transition:
			glColor4f(1.0f, 0.65f, 0.15f, statusAlpha);
			break;

		case WorkspaceStatusTone::Neutral:
		default:
			glColor4f(0.72f, 0.78f, 0.82f, m_panelSlide);
			break;
		}

		drawText2D(
			panelX(rowX),
			y,
			presentation.statusLine.c_str(),
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
void ViewPort::drawOverlay(const WorkspacePresentation& presentation) {

	updatePanelAnimation(presentation.panelVisible);
	beginOverlay2D();

	drawWorkspaceFrame(0.30f, nullptr);

	if (m_panelSlide > 0.0f) {

		drawPanelBackground();
		drawPresentationHeader(presentation);
		drawPresentationSections(presentation);
		drawPresentationFooter(presentation);
	}

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	endOverlay2D();
}
