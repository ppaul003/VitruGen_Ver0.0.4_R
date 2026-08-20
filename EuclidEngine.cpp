#ifdef _WIN32
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <vector>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <string>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "kernel.h"
#include "EuclidEngine.h"
//#include "ParticleSimRuntimeConfig.h"
#include "PngImage.h"

using namespace std;
using namespace glm;

namespace fs = std::filesystem;

EuclidEngine* EuclidEngine::s_instance = nullptr;

#ifdef _WIN32

namespace {

	HICON g_vitruGenLargeIcon = nullptr;
	HICON g_vitruGenSmallIcon = nullptr;

	bool applyVitruGenIconFromFile(
		const char* iconFilename) {

		if (!iconFilename || iconFilename[0] == '\0') {

			return false;
		}

		// Immediately after glutCreateWindow(), this should
		// resolve to the FreeGLUT window.
		HWND windowHandle = GetActiveWindow();

		// Fallback lookup using the original window title.
		if (!windowHandle) {

			windowHandle =
				FindWindowA(nullptr, "VitruGen Ver0.0.4");
		}

		if (!windowHandle) {

			printf(
				"[EuclidEngine] WARNING: "
				"Could not locate the VitruGen window.\n"
			);

			return false;
		}

		const int largeWidth = GetSystemMetrics(SM_CXICON);
		const int largeHeight = GetSystemMetrics(SM_CYICON);
		const int smallWidth = GetSystemMetrics(SM_CXSMICON);
		const int smallHeight = GetSystemMetrics(SM_CYSMICON);

		g_vitruGenLargeIcon =
			static_cast<HICON>(
				LoadImageA(
					nullptr,
					iconFilename,
					IMAGE_ICON,
					largeWidth,
					largeHeight,
					LR_LOADFROMFILE
				)
			);

		g_vitruGenSmallIcon =
			static_cast<HICON>(
				LoadImageA(
					nullptr,
					iconFilename,
					IMAGE_ICON,
					smallWidth,
					smallHeight,
					LR_LOADFROMFILE
				)
			);

		if (!g_vitruGenLargeIcon &&
			!g_vitruGenSmallIcon) {

			printf(
				"[EuclidEngine] WARNING: "
				"Could not load app icon: %s\n",
				iconFilename
			);

			return false;
		}

		if (g_vitruGenLargeIcon) {

			SendMessageA(
				windowHandle,
				WM_SETICON,
				ICON_BIG,
				reinterpret_cast<LPARAM>(g_vitruGenLargeIcon)
			);
		}

		if (g_vitruGenSmallIcon) {

			SendMessageA(
				windowHandle,
				WM_SETICON,
				ICON_SMALL,
				reinterpret_cast<LPARAM>(g_vitruGenSmallIcon)
			);
		}

		printf(
			"[EuclidEngine] Application icon loaded: %s\n",
			iconFilename
		);

		return true;
	}

	void releaseVitruGenWindowIcons() {

		if (g_vitruGenLargeIcon) {

			DestroyIcon(g_vitruGenLargeIcon);
			g_vitruGenLargeIcon = nullptr;
		}

		if (g_vitruGenSmallIcon) {

			DestroyIcon(g_vitruGenSmallIcon);
			g_vitruGenSmallIcon = nullptr;
		}
	}

} // namespace

#endif

// =============================================================================
// LIFECYCLE
// =============================================================================

EuclidEngine::EuclidEngine() {
}

EuclidEngine::~EuclidEngine() { 
	shutdown(); 
}

bool EuclidEngine::init(int argc, char** argv) {

	printf("Anaheim Systems Dynamics Inc.\n");
	printf("VitruGen Starting... \n");
	printf("Initializing generic VitruGen host architecture...\n");
	printf("Welcome to the Tesseract Generator Matrix! \n\n");

	s_instance = this;
	
	// ---------------------------------------------------------
	// 1. Create OpenGL / GLUT host.
	// ---------------------------------------------------------
	initGL(&argc, argv);
	cudaGLInit(argc, argv);

	// ---------------------------------------------------------
	// 2. Renderer MUST be created after the GL context exists.
	//
	// EuclidRenderer constructor initializes shader programs
	// and GPU resources.
	// ---------------------------------------------------------
	initRenderer();
	
	// ---------------------------------------------------------
	// 3. Wire the workspace socket.
	// ---------------------------------------------------------
	if (!initWorkspaceHost()) {

		printf(
			"[EuclidEngine] ERROR: "
			"Workspace host initialization failed.\n"
		);

		return false;
	}

	// ---------------------------------------------------------
	// 4. GLUT runtime callbacks.
	// ---------------------------------------------------------
	glutDisplayFunc(&EuclidEngine::sDisplay);
	glutReshapeFunc(&EuclidEngine::sReshape);
	glutMouseFunc(&EuclidEngine::sMouse);
	glutMotionFunc(&EuclidEngine::sMotion);
	glutPassiveMotionFunc(&EuclidEngine::sPassiveMotion);
	glutKeyboardFunc(&EuclidEngine::sKeyboard);
	glutIdleFunc(&EuclidEngine::sIdle);
	
	glutCloseFunc(&EuclidEngine::sClose);

	return true;
}

void EuclidEngine::initGL(int* argc, char** argv) {
	glutInit(argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutInitWindowSize(kWidth, kHeight);
	glutCreateWindow("VitruGen Ver0.0.4");

#ifdef _WIN32

	applyVitruGenIconFromFile("VitruGen_Ver004.ico");

#endif

	// ---------------------------------------------------------
	// GLEW must initialize after the GL context exists.
	// ---------------------------------------------------------

	glewExperimental = GL_TRUE;
	const GLenum glewResult = glewInit();

	if (glewResult != GLEW_OK) {

		printf(
			"[EuclidEngine] WARNING: "
			"GLEW initialization failed: %s\n",
			glewGetErrorString(glewResult)
		);
	}

	glEnable(GL_DEPTH_TEST);
	glClearColor(0.05f, 0.05f, 0.15f, 1.0f);

	m_viewport.resize(kWidth, kHeight);
	m_viewport.applyPerspective(60.0f);

	initMenus();
}

void EuclidEngine::initRenderer() {
	if (m_renderer) return;

	m_renderer = new EuclidRenderer();

	m_renderer->setWindowSize(
		m_viewport.getWidth(),
		m_viewport.getHeight()
	);

	m_renderer->setFOV(60.0f);

	m_renderer->setSimBoxSize(4);
	m_renderer->setGridDimSize(64);
	m_renderer->setGridMajorEvery(8);

	printf(
		"[EuclidEngine] EuclidRenderer initialized.\n"
	);
}

// --- SOCKET --- //
bool EuclidEngine::initWorkspaceHost() {
	if (!m_renderer) {

		printf(
			"[EuclidEngine] ERROR: "
			"Cannot initialize workspace host "
			"without EuclidRenderer.\n"
		);

		return false;
	}

	WorkspaceServices services;
	services.renderer = m_renderer;
	services.arbiter = &m_arbiter;
	services.viewport = &m_viewport;

	// ---------------------------------------------------------
	// Establish host structural state.
	// ---------------------------------------------------------
	m_arbiter.setApplicationLayer(TheArbiter::ApplicationLayer::GLOBAL_SHELL);
	m_arbiter.setWorkspaceDomain(TheArbiter::WorkspaceDomain::NONE);
	m_arbiter.setActiveWorkspace(TheArbiter::WorkspaceId::DIAGNOSTIC);

	// ---------------------------------------------------------
	// Insert first cartridge.
	// ---------------------------------------------------------
	if (!m_tesseract.initialize(services)) {

		return false;
	}

	printf(
		"[EuclidEngine] Workspace services wired.\n"
	);

	return true;
}

WorkspaceFrameContext
EuclidEngine::buildWorkspaceFrameContext(float deltaTime) const {

	WorkspaceFrameContext frame;
	frame.deltaTime = deltaTime;

	frame.elapsedTime =
		static_cast<float>(glutGet(GLUT_ELAPSED_TIME)) * 0.001f;

	frame.viewportWidth = m_viewport.getWidth();
	frame.viewportHeight = m_viewport.getHeight();

	frame.thetaRad = 0.0f;
	frame.phiRad = 0.0f;
	frame.zoom = 1.0f;

	frame.displayEnabled = m_displayEnabled;

	return frame;
}

void EuclidEngine::initMenus() {
	glutDetachMenu(GLUT_RIGHT_BUTTON);

	if (m_menuId != 0) {
		
		glutDestroyMenu(m_menuId);
		m_menuId = 0;
	}

	m_menuId =
		glutCreateMenu(&EuclidEngine::sMainMenu);

	glutAddMenuEntry(
		"=========================================",
		MENU_NOP
	);

	glutAddMenuEntry(
		"- VitruGen Tesseract Behavioral Object -",
		MENU_NOP
	);

	glutAddMenuEntry(
		"=========================================",
		MENU_NOP
	);

	glutAddMenuEntry(
		"* Quit (esc)",
		MENU_QUIT
	);

	glutAddMenuEntry(
		"=========================================",
		MENU_NOP
	);

	glutAttachMenu(
		GLUT_RIGHT_BUTTON
	);
}

void EuclidEngine::run() { 
	if (m_exiting) return;

	glutMainLoop(); 
}

void EuclidEngine::shutdown() {
	if (m_cleaned) return;
	m_cleaned = true;

	printf(
		"[EuclidEngine] Shutting down...\n"
	);

	if (m_renderer) {
		delete m_renderer;
		m_renderer = nullptr;
	}

	if (m_objExportFutureActive && m_objExportFuture.valid()) {
		m_objExportFuture.wait();
		m_objExportFutureActive = false;
	}

	if (m_staticAssetFutureActive && m_staticAssetFuture.valid()) {
		m_staticAssetFuture.wait();
		m_staticAssetFutureActive = false;
	}

#ifdef _WIN32

	releaseVitruGenWindowIcons();
#endif

	s_instance = nullptr;
}

void EuclidEngine::computeFPS() {
	static int frameCount = 0;
	static int previousTime = glutGet(GLUT_ELAPSED_TIME);

	frameCount++;

	const int currentTime = glutGet(GLUT_ELAPSED_TIME);
	const int elapsed = currentTime - previousTime;

	if (elapsed < 1000)
		return;

	const float fps =
		static_cast<float>(frameCount) *
		1000.0f /
		static_cast<float>(elapsed);

	char title[128];

	snprintf(
		title,
		sizeof(title),
		"VitruGen Ver0.0.4 : %.1f fps",
		fps
	);

	glutSetWindowTitle(title);
}

void EuclidEngine::requestExit() {
	if (m_exiting) return;

	m_exiting = true;

	printf(
		"[EuclidEngine] Exit requested.\n"
	);

	glutLeaveMainLoop();
}

// =============================================================================
// CONTEXT MENU PRESENTATION / COMMAND ROUTING
// =============================================================================
void EuclidEngine::rebuildMenus() {

	initMenus();
}

// =============================================================================
// GLUT CALLBACK BRIDGE
// =============================================================================
void EuclidEngine::sDisplay() {
	if (s_instance) s_instance->onDisplay();
}

void EuclidEngine::sReshape(int w, int h) {
	if (s_instance) s_instance->onReshape(w, h);
}

void EuclidEngine::sMouse(int b, int s, int x, int y) {
	if (s_instance) s_instance->onMouse(b, s, x, y);
}

void EuclidEngine::sMotion(int x, int y) {
	if (s_instance) s_instance->onMotion(x, y);
}

void EuclidEngine::sPassiveMotion(int x, int y) {
	if (s_instance) 
		s_instance->onPassiveMotion(x, y);
}

void EuclidEngine::sMainMenu(int value) {

	if (!s_instance) return;
	if (value == MENU_NOP) return;

	// Feed menu commands through the same raw-to-generic path
	// used by the keyboard.  Layer 0 currently exposes only the
	// host-generic ESC/Quit command.
	s_instance->onKeyboard(
		static_cast<unsigned char>(value),
		0,
		0
	);
}

void EuclidEngine::sMenuStatus(int status, int x, int y) {

	// Reserved for future generic workspace menus.
}

void EuclidEngine::sKeyboard(unsigned char k, int x, int y) {
	if (s_instance) s_instance->onKeyboard(k, x, y);
}

void EuclidEngine::sIdle() {
	if (s_instance) s_instance->onIdle();
}

void EuclidEngine::sClose() {
	if (s_instance) s_instance->onClose();
}

// =============================================================================
// RUNTIME EVENT HANDLERS
// =============================================================================
void EuclidEngine::onReshape(int w, int h) {

	m_viewport.resize(w, h);
	m_viewport.applyPerspective(60.0f);

	if (m_renderer) {
		m_renderer->setWindowSize(w, h);
		m_renderer->setFOV(60.0f);
	}

	glutPostRedisplay();
}

void EuclidEngine::onDisplay() {
	// ---------------------------------------------------------
	// Clear host framebuffer.
	// ---------------------------------------------------------
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// ---------------------------------------------------------
	// Establish 3D projection.
	// ---------------------------------------------------------
	m_viewport.applyPerspective(60.0f);

	// ---------------------------------------------------------
	// Temporary R-A0 diagnostic camera.
	//
	// This is deliberately simple.
	// We can wire CameraProcessor after HOST POST succeeds.
	// ---------------------------------------------------------
	const WorkspaceFrameContext frame =
		buildWorkspaceFrameContext(0.0f);

	const float previewRotation = frame.elapsedTime * 25.0f;

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// ---------------------------------------------------------
	// GOLD Layer-0 menu camera behavior.
	//
	// GOLD:
	//     X translation = +1.65
	//     Z translation = -8.0
	//     pitch         = 18 degrees
	//     yaw           = previewRotation
	//
	// CameraProcessor already contains this behavior.
	// ---------------------------------------------------------
	m_camera.setBehaviorMode(CameraProcessor::CAM_MENU_PREVIEW);

	m_camera.updateLag();
	m_camera.applyMenuCameraTransform(previewRotation);

	// ---------------------------------------------------------
	// Render active cartridge.
	// ---------------------------------------------------------
	m_tesseract.render(frame);

	// ---------------------------------------------------------
	// Render generic workspace presentation.
	// ---------------------------------------------------------
	m_viewport.drawOverlay(m_tesseract.presentation());

	glutSwapBuffers();
}

void EuclidEngine::onMouse(int button, int state, int x, int y) {
	
	const TheArbiter::ArbiterResult result =
		m_arbiter.routeMouseButton(button, state, x, y);

	if (result.hasWorkspaceInput) {

		m_tesseract.handleInput(result.workspaceInput);
	}

	if (result.requestRedraw) {

		glutPostRedisplay();
	}
}

void EuclidEngine::onMotion(int x, int y) {
	
	const TheArbiter::ArbiterResult result =
		m_arbiter.routePointerMove(x, y);

	if (result.hasWorkspaceInput) {

		m_tesseract.handleInput(result.workspaceInput);
	}

	glutPostRedisplay();
}

void EuclidEngine::onPassiveMotion(int x, int y) {
	
	const TheArbiter::ArbiterResult result =
		m_arbiter.routePointerMove(x, y);

	if (result.hasWorkspaceInput) {

		m_tesseract.handleInput(result.workspaceInput);
	}
}

void EuclidEngine::onKeyboard(unsigned char key, int x, int y) {

	const KeyboardInput::KeyEvent event =
		m_keyboard.onKey(key, x, y);

	const TheArbiter::ArbiterResult result =
		m_arbiter.routeKeyboard(event);

	// ---------------------------------------------------------
	// Host command.
	// ---------------------------------------------------------
	if (result.arbiterCommand == TheArbiter::ArbiterCommand::CMD_EXIT) {

		requestExit();
		return;
	}

	// ---------------------------------------------------------
	// Workspace command.
	// ---------------------------------------------------------
	if (result.hasWorkspaceInput) {
		if (m_tesseract.handleInput(result.workspaceInput)) {
			
			glutPostRedisplay();
		}
	}

	if (result.requestRedraw) {
		
		glutPostRedisplay();
	}
	
	if (result.rebuildMenu) {

		rebuildMenus();
	}
}

void EuclidEngine::onIdle() {
	
	static int previousTimeMs =
		glutGet(GLUT_ELAPSED_TIME);

	const int currentTimeMs =
		glutGet(GLUT_ELAPSED_TIME);

	const int elapsedMs =
		currentTimeMs - 
		previousTimeMs;

	previousTimeMs = currentTimeMs;

	float deltaTime =
		static_cast<float>(elapsedMs) * 0.001f;

	// ---------------------------------------------------------
	// Prevent giant animation jumps after breakpoints,
	// window stalls, etc.
	// ---------------------------------------------------------
	deltaTime = std::min(deltaTime, 0.050f);

	const WorkspaceFrameContext frame =
		buildWorkspaceFrameContext(deltaTime);

	m_tesseract.update(frame);

	computeFPS();
	glutPostRedisplay();
}

void EuclidEngine::onClose() {

	shutdown();
}
