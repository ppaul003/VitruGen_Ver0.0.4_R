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
				FindWindowA(nullptr, "anaheim");
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
	services.camera = &m_camera;

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

	const float degreesToRadians = 0.01745329251994329577f;
	const float* rotation = m_camera.getLaggedRotation();
	frame.phiRad = rotation[0] * degreesToRadians;
	frame.thetaRad = rotation[1] * degreesToRadians;
	frame.zoom = 1.0f;
	frame.particleWorkspaceZs = m_camera.getParticleWorkspaceZs();
	frame.volumeRenderZs = m_camera.getVolumeRenderZs();

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
	m_workspaceMenuCommands.clear();

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

	const WorkspaceMenuPresentation workspaceMenu = m_tesseract.menu();
	for (const WorkspaceMenuItem& item : workspaceMenu.items) {
		const int slot = static_cast<int>(m_workspaceMenuCommands.size());
		m_workspaceMenuCommands.push_back(item.command);
		glutAddMenuEntry(item.label.c_str(), item.enabled
			? MENU_WORKSPACE_COMMAND_BASE + slot : MENU_NOP);
	}
	if (!workspaceMenu.items.empty())
		glutAddMenuEntry("=========================================", MENU_NOP);

	glutAddMenuEntry(
		"* Quit (esc)",
		MENU_QUIT
	);

	glutAddMenuEntry(
		"=========================================",
		MENU_NOP
	);

	if (!isAnyHostModalActive())
		glutAttachMenu(GLUT_RIGHT_BUTTON);
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

	if (m_objExportFutureActive && m_objExportFuture.valid()) {
		m_objExportFuture.wait();
		m_objExportFutureActive = false;
	}

	if (m_staticAssetFutureActive && m_staticAssetFuture.valid()) {
		m_staticAssetFuture.wait();
		m_staticAssetFutureActive = false;
	}

	m_tesseract.shutdown();

	if (m_renderer) {
		delete m_renderer;
		m_renderer = nullptr;
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
	if (s_instance->isAnyHostModalActive()) return;
	if (value == MENU_NOP) return;
	if (value >= MENU_WORKSPACE_COMMAND_BASE) {
		const int slot = value - MENU_WORKSPACE_COMMAND_BASE;
		if (slot >= 0 && slot < static_cast<int>(
			s_instance->m_workspaceMenuCommands.size())) {
			s_instance->m_tesseract.handleMenuCommand(
				s_instance->m_workspaceMenuCommands[slot]);
			s_instance->consumeWorkspaceHostRequest();
			s_instance->rebuildMenus();
			glutPostRedisplay();
		}
		return;
	}

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

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// ---------------------------------------------------------
	// Update camera smoothing.
	//
	// Camera BEHAVIOR is selected by the current workspace /
	// Tesseract transition controller.
	//
	// EuclidEngine only advances and applies the camera.
	// ---------------------------------------------------------
	m_camera.updateLag();
	m_camera.updatePocketZoomLag();
	
	// ---------------------------------------------------------
	// Apply current camera pose.
	//
	// During GRID_3D transitions the Arbiter deliberately keeps
	// the structural layer unchanged until choreography ends:
	//
	// Forward:
	//     GLOBAL_SHELL remains active until camera transition
	//     finishes.
	//
	// Reverse:
	//     DOMAIN_SELECTION remains active until return animation
	//     finishes.
	//
	// The transition controller modifies the camera pose itself.
	// ---------------------------------------------------------
	if (m_arbiter.isGlobalShell()) {
		m_camera.applyMenuCameraTransform();
	}
	else {
		m_camera.applyViewCameraTransform();
	}

	const WorkspaceFrameContext frame =
		buildWorkspaceFrameContext(0.0f);

	// ---------------------------------------------------------
	// Render active cartridge.
	// ---------------------------------------------------------
	m_tesseract.render(frame);

	const WorkspacePresentation presentation =
		m_tesseract.presentation();

	m_viewport.drawOverlay(
		presentation,
		isObjExportModalActive()
		? &m_objExportPanel
		: isStaticParticleAssetModalActive()
		? &m_staticAssetPanel : nullptr
	);

	glutSwapBuffers();
}

void EuclidEngine::onMouse(int button, int state, int x, int y) {
	if (isAnyHostModalActive()) return;
	// ---------------------------------------------------------
	// Layer 0 / domain transitions are keyboard-driven.
	//
	// Match GOLD behavior:
	//     no orbit
	//     no wheel zoom
	//     no workspace mouse interaction
	// ---------------------------------------------------------
	if (m_arbiter.isGlobalShell() || m_tesseract.workspaceInputLocked())
		return;

	const bool isWheel = button == 3 || button == 4;
	const TheArbiter::ArbiterResult result =
		m_arbiter.routeMouseButton(button, state, x, y);

	bool workspaceHandled = false;
	if (result.hasWorkspaceInput) {
		workspaceHandled = m_tesseract.handleInput(result.workspaceInput);
	}

	if (!workspaceHandled && isWheel) {
		switch (m_camera.getBehaviorMode()) {
		case CameraProcessor::CAM_SINGLE_PARTICLE_VOLUME:
		case CameraProcessor::CAM_SINGLE_PARTICLE_MARCHING_CUBES:
			m_camera.zoomVolumeRenderByWheel(button);
			break;
		case CameraProcessor::CAM_SINGLE_PARTICLE_ORBIT_CLOSE:
		case CameraProcessor::CAM_SINGLE_PARTICLE_WORKPLANE_LOCKED:
			m_camera.zoomParticleWorkspaceByWheel(button);
			break;
		default:
			m_camera.zoomByWheel(button);
			break;
		}
		workspaceHandled = true;
	}

	if (!workspaceHandled) {
		workspaceHandled = m_mouse.onButton(
			button, state, x, y, m_camera.getTranslation());
	}

	if (workspaceHandled || result.requestRedraw) {

		glutPostRedisplay();
	}
}

void EuclidEngine::onMotion(int x, int y) {

	if (isAnyHostModalActive())
		return;

	const TheArbiter::ArbiterResult result =
		m_arbiter.routePointerMove(x, y);

	bool workspaceHandled = false;
	if (result.hasWorkspaceInput) {
		workspaceHandled = m_tesseract.handleInput(result.workspaceInput);
	}

	if (!workspaceHandled && m_camera.orbitEnabled()) {
		m_mouse.onMotion(
			x,
			y,
			m_camera.getRotation(),
			m_camera.getOrbitSensitivityScale());
	}

	glutPostRedisplay();
}

void EuclidEngine::onPassiveMotion(int x, int y) {
	if (isAnyHostModalActive()) return;
	if (m_arbiter.isGlobalShell() ||
		m_tesseract.workspaceInputLocked()) return;

	const TheArbiter::ArbiterResult result =
		m_arbiter.routePointerMove(x, y);

	if (result.hasWorkspaceInput) {

		m_tesseract.handleInput(result.workspaceInput);
	}

	m_mouse.onPassiveMotion(x, y);
}

void EuclidEngine::onKeyboard(unsigned char key, int x, int y) {

	const KeyboardInput::KeyEvent event =
		m_keyboard.onKey(key, x, y);

	// GOLD modal priority: OBJ, then named asset persistence,
	// then any remaining generic host workflow.
	if (handleObjExportModalKeyboard(event)) {
		glutPostRedisplay();
		return;
	}

	// ---------------------------------------------------------
	// GOLD static asset modal has absolute input priority.
	// ---------------------------------------------------------
	if (handleStaticParticleAssetModalKeyboard(event)) {
		glutPostRedisplay();
		return;
	}

	// Existing generic modal remains available for workflows
	// we haven't migrated yet.
	if (handleHostModalKeyboard(key)) {
		glutPostRedisplay();
		return;
	}

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
			consumeWorkspaceHostRequest();
			rebuildMenus();
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

	const int elapsedMs = currentTimeMs - previousTimeMs;
	previousTimeMs = currentTimeMs;

	float deltaTime =
		static_cast<float>(elapsedMs) * 0.001f;

	// ---------------------------------------------------------
	// Prevent giant animation jumps after breakpoints,
	// window stalls, etc.
	// ---------------------------------------------------------
	deltaTime = (std::min)(deltaTime, 0.050f);

	// ---------------------------------------------------------
	// Advance host-side asynchronous persistence jobs.
	//
	// StaticParticleAssetIO performs filesystem / CPU loading
	// in its worker thread. Completion is consumed here on the
	// GLUT/main thread so renderer/CUDA resources can safely be
	// updated.
	// ---------------------------------------------------------
	advanceStaticParticleAssetJob();
	advanceObjExportJob();

	const WorkspaceFrameContext frame =
		buildWorkspaceFrameContext(deltaTime);

	m_tesseract.update(frame);
	consumeWorkspaceHostRequest();

	computeFPS();
	glutPostRedisplay();
}

void EuclidEngine::onClose() {

	shutdown();
}

void EuclidEngine::openStaticParticleLoadPanel() {
	if (isAnyHostModalActive() ||
		m_staticAssetFutureActive) return;

	m_staticAssetCatalog =
		vitru::enumerateStaticParticleAssets(m_inputsRoot, m_outputRoot / "STATIC_PARTICLES");

	vector<vitru::StaticAssetCatalogEntry> valid;

	for (const auto& entry : m_staticAssetCatalog) {
		if (entry.valid) valid.push_back(entry);
	}

	m_staticAssetCatalog.swap(valid);
	m_staticAssetPanel = ViewPort::ObjExportPanelData{};
	m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::SELECT;

	m_staticAssetPanel.titleText =
		"VITRUGEN STATIC PARTICLE LOAD";

	m_staticAssetPanel.selectedIndex = 0;

	for (const auto& entry : m_staticAssetCatalog) {
		m_staticAssetPanel.selectionLines.push_back(
			entry.displayName + " | " +
			entry.source + " | VALID | " +
			entry.manifestPath.generic_string()
		);
	}

	m_staticAssetJobKind = StaticAssetJobKind::Load;
	glutDetachMenu(GLUT_RIGHT_BUTTON);

	glutPostRedisplay();
}

void EuclidEngine::openStaticParticleSaveAsPanel() {
	if (isAnyHostModalActive() || m_staticAssetFutureActive) return;

	m_hostTextEntry.beginAssetName(
		"STATIC PARTICLE ASSET NAME", "", 64);
	m_staticAssetPanel = ViewPort::ObjExportPanelData{};
	m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::NAME_ENTRY;
	m_staticAssetPanel.titleText = "VITRUGEN STATIC PARTICLE SAVE AS";
	m_staticAssetPanel.promptText = m_hostTextEntry.getPrompt();
	m_staticAssetPanel.inputText = m_hostTextEntry.getBuffer();
	m_staticAssetPanel.statusText = m_hostTextEntry.getStatusMessage();
	m_staticAssetJobKind = StaticAssetJobKind::Save;
	glutDetachMenu(GLUT_RIGHT_BUTTON);
	glutPostRedisplay();
}

void EuclidEngine::openStaticParticleSaveConfirm(
	const std::string& displayName) {
	if (isObjExportModalActive() || m_staticAssetFutureActive) return;
	vitru::StaticParticleAsset asset;
	if (!makeCurrentStaticParticleAsset(asset)) {
		m_staticAssetPanel = ViewPort::ObjExportPanelData{};
		m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
		m_staticAssetPanel.titleText = "VITRUGEN STATIC PARTICLE SAVE";
		m_staticAssetPanel.statusText = "No canonical mesh or active asset.";
		m_staticAssetPanel.logLines.push_back("SAVE disabled: no valid StaticParticleAsset is active.");
		return;
	}
	if (displayName.empty()) {
		const vitru::StaticParticleAsset* active =
			m_assetRepository.activeStaticParticle();
		if (!active || active->name.empty()) {
			m_staticAssetPanel = ViewPort::ObjExportPanelData{};
			m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
			m_staticAssetPanel.titleText = "VITRUGEN STATIC PARTICLE SAVE";
			m_staticAssetPanel.statusText = "Use SAVE STATIC PARTICLE AS first.";
			m_staticAssetPanel.logLines.push_back("SAVE disabled: active asset has no named output location.");
			return;
		}
		m_pendingStaticAssetName = active->name;
	}
	else {
		m_pendingStaticAssetName = displayName;
	}
	m_staticAssetPanel = ViewPort::ObjExportPanelData{};
	m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::CONFIRM;
	m_staticAssetPanel.titleText = "VITRUGEN STATIC PARTICLE SAVE";
	m_staticAssetPanel.confirmText = "Save named asset: " + m_pendingStaticAssetName + " ?";
	m_staticAssetPanel.yesSelected = true;
	m_staticAssetJobKind = StaticAssetJobKind::Save;
	glutDetachMenu(GLUT_RIGHT_BUTTON);
	glutPostRedisplay();
}

void EuclidEngine::beginStaticParticleAssetJob() {
	if (m_staticAssetFutureActive) return;
	StaticAssetJobKind kind = m_staticAssetJobKind;
	vitru::StaticParticleAsset source;
	fs::path manifest;
	if (kind == StaticAssetJobKind::Save) {
		if (!makeCurrentStaticParticleAsset(source)) {
			m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
			m_staticAssetPanel.statusText = "Active asset validation failed.";
			return;
		}
	}
	else if (kind == StaticAssetJobKind::Load) {
		if (m_staticAssetCatalog.empty() || m_staticAssetPanel.selectedIndex < 0 ||
			m_staticAssetPanel.selectedIndex >= static_cast<int>(m_staticAssetCatalog.size())) return;
		manifest = m_staticAssetCatalog[static_cast<size_t>(m_staticAssetPanel.selectedIndex)].manifestPath;
	}
	else return;

	m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::WORKING;
	m_staticAssetPanel.progressPercent = 10;
	m_staticAssetPanel.spinnerFrame = 0;
	m_staticAssetPanel.statusText = kind == StaticAssetJobKind::Save
		? "validating mesh" : "reading VSPA";
	m_staticAssetPanel.logLines.clear();
	m_staticAssetPanel.logLines.push_back(kind == StaticAssetJobKind::Save
		? "[VSPA] validating mesh and material resources"
		: "[VSPA] reading selected manifest");
	const fs::path outputRoot = m_outputRoot;
	const fs::path workspace = m_workspaceObj;
	const std::string name = m_pendingStaticAssetName;
	m_staticAssetFuture = std::async(std::launch::async,
		[kind, source, manifest, outputRoot, workspace, name]() mutable {
			StaticAssetAsyncResult result;
			if (kind == StaticAssetJobKind::Save) {
				result.success = vitru::saveStaticParticleBundle(
					source, outputRoot, name, workspace, result.report);
				if (result.success) {
					vitru::StaticAssetOperationReport reopened;
					result.success = vitru::loadStaticParticleBundle(
						result.report.manifestPath, result.asset, reopened,
						nullptr, workspace);
					result.report.warnings.insert(result.report.warnings.end(),
						reopened.warnings.begin(), reopened.warnings.end());
					result.report.errors.insert(result.report.errors.end(),
						reopened.errors.begin(), reopened.errors.end());
				}
			}
			else {
				result.success = vitru::loadStaticParticleBundle(
					manifest, result.asset, result.report, nullptr, workspace);
			}
			return result;
		});
	m_staticAssetFutureActive = true;
	m_staticAssetLastSpinnerMs = glutGet(GLUT_ELAPSED_TIME);
}

void EuclidEngine::advanceStaticParticleAssetJob() {
	if (!m_staticAssetFutureActive || 
		!m_staticAssetFuture.valid()) return;

	const int now = glutGet(GLUT_ELAPSED_TIME);

	// ---------------------------------------------------------
	// GOLD-style visible progress / spinner.
	// ---------------------------------------------------------
	if (now - m_staticAssetLastSpinnerMs >= 100) {

		m_staticAssetPanel.spinnerFrame =
			(m_staticAssetPanel.spinnerFrame + 1) % 4;

		m_staticAssetPanel.progressPercent =
			(std::min)(90, m_staticAssetPanel.progressPercent + 1);

		m_staticAssetLastSpinnerMs = now;
	}

	// ---------------------------------------------------------
	// Filesystem / CPU asset load still running.
	// ---------------------------------------------------------
	if (m_staticAssetFuture.wait_for(chrono::milliseconds(0)) !=
		future_status::ready) return;

	// ---------------------------------------------------------
	// Worker completed.
	// ---------------------------------------------------------
	StaticAssetAsyncResult result = m_staticAssetFuture.get();
	m_staticAssetFutureActive = false;

	for (const string& warning : result.report.warnings)
		m_staticAssetPanel.logLines.push_back("[WARN] " + warning);

	for (const string& error : result.report.errors)
		m_staticAssetPanel.logLines.push_back("[ERROR] " + error);

	if (!result.success) {
		m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
		m_staticAssetPanel.statusText = result.report.phase;
		return;
	}

	// =========================================================
	// INSTALL CANONICAL ASSET
	// =========================================================
	const vitru::AssetId id = m_assetRepository.addStaticParticle(result.asset);
	m_assetRepository.setActiveStaticParticle(id);

	vitru::StaticParticleAsset* active =
		m_assetRepository.activeStaticParticle();

	if (!active) {
		
		m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
		m_staticAssetPanel.statusText = "Active asset repository commit failed.";

		return;
	}
	
	// =========================================================
	// GPU MESH / MATERIAL / TEXTURE UPLOAD
	//
	// MUST happen on GLUT / GL main thread.
	// =========================================================
	if (!m_renderer || !m_renderer->loadParticleStaticAsset(*active)) {

		m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
		m_staticAssetPanel.statusText = "GPU asset upload failed.";

		m_staticAssetPanel.logLines.push_back(
			"[ERROR] renderer could not "
			"upload the loaded asset"
		);

		return;
	}

	// ---------------------------------------------------------
	// Restore the optional native scalar field on the main thread.
	//
	// Bundle parsing and binary file reading happened in the async
	// worker. CUDA upload happens here, where the application CUDA/GL
	// context is active.
	// ---------------------------------------------------------
	bool editableVolumeRestored = false;

	if (active->volumetricSource.available) {

		const int3 sourceDimensions =
			make_int3(
				static_cast<int>(active->volumetricSource.dimensions[0]),
				static_cast<int>(active->volumetricSource.dimensions[1]),
				static_cast<int>(active->volumetricSource.dimensions[2])
			);

		if (!m_tesseract.restoreSingleParticleVolume(active->volumetricSource.samples, sourceDimensions)) {
			
			m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
			m_staticAssetPanel.statusText = "Native FLOAT32_SDF GPU restore failed.";

			m_staticAssetPanel.logLines.push_back(
				"[ERROR] native volume "
				"could not be restored"
			);
			
			return;
		}

		editableVolumeRestored = true;

		m_staticAssetPanel.logLines.push_back(
			"[VSPA] native FLOAT32_SDF "
			"volume restored"
		);
	}

	// =========================================================
	// ACTIVATE LOADED SINGLE_PARTICLE
	//
	// This is the reconstructed equivalent of GOLD's
	// applySingleParticleConfigToSystem() path.
	//
	// It switches the cartridge to:
	//
	//     LoadedStaticMesh
	//     ParticleRenderMode::Mesh
	//
	// without returning workspace semantics to EuclidEngine.
	// =========================================================
	m_tesseract.activateLoadedSingleParticleBase(editableVolumeRestored);

	// =========================================================
	// COMPLETE
	// =========================================================
	m_staticAssetPanel.mode = ViewPort::ObjExportPanelMode::COMPLETE;
	m_staticAssetPanel.progressPercent = 100;

	m_staticAssetPanel.statusText =
		editableVolumeRestored
		? "geometry, materials, textures, p0 and native volume ready"
		: "geometry, materials, textures and p0 ready";

	m_staticAssetPanel.logLines.push_back("[VSPA] active repository and p0.obj refreshed");
	m_staticAssetPanel.logLines.push_back("[EuclidRenderer] textured material ranges uploaded");
	
	rebuildMenus();
	glutPostRedisplay();
}

void EuclidEngine::closeStaticParticleAssetPanel() {
	if (m_staticAssetFutureActive) return;
	m_staticAssetPanel = ViewPort::ObjExportPanelData{};
	m_staticAssetCatalog.clear();
	m_staticAssetJobKind = StaticAssetJobKind::None;
	m_pendingStaticAssetName.clear();
	m_hostTextEntry.reset();
	rebuildMenus();
	glutPostRedisplay();
}

bool EuclidEngine::handleStaticParticleAssetModalKeyboard(const KeyboardInput::KeyEvent& event) {

	if (!isStaticParticleAssetModalActive()) return false;
	using Mode = ViewPort::ObjExportPanelMode;

	if (m_staticAssetPanel.mode == Mode::NAME_ENTRY) {
		const TextEntryAction action =
			m_hostTextEntry.handleRawKey(event.rawKey);
		m_staticAssetPanel.inputText = m_hostTextEntry.getBuffer();
		m_staticAssetPanel.statusText = m_hostTextEntry.getStatusMessage();
		if (action == TextEntryAction::Committed) {
			const std::string name = m_hostTextEntry.getNormalizedText();
			m_hostTextEntry.reset();
			openStaticParticleSaveConfirm(name);
		}
		else if (action == TextEntryAction::Cancelled) {
			closeStaticParticleAssetPanel();
		}
		glutPostRedisplay();
		return true;
	}

	// --- SELECT --- //
	if (m_staticAssetPanel.mode == Mode::SELECT) {

		if (event.signal == KeyboardInput::KEY_W && !m_staticAssetCatalog.empty())
			m_staticAssetPanel.selectedIndex = (m_staticAssetPanel.selectedIndex - 1 + static_cast<int>(m_staticAssetCatalog.size())) % static_cast<int>(m_staticAssetCatalog.size());
		else if (event.signal == KeyboardInput::KEY_S && !m_staticAssetCatalog.empty())
			m_staticAssetPanel.selectedIndex = (m_staticAssetPanel.selectedIndex + 1) % static_cast<int>(m_staticAssetCatalog.size());
		else if ((event.signal == KeyboardInput::KEY_E || event.signal == KeyboardInput::KEY_ENTER) && !m_staticAssetCatalog.empty()) beginStaticParticleAssetJob();
		else if (event.signal == KeyboardInput::KEY_Q || event.signal == KeyboardInput::KEY_ESCAPE) closeStaticParticleAssetPanel();
		glutPostRedisplay(); return true;
	}

	// --- CONFIRM --- //
	if (m_staticAssetPanel.mode == Mode::CONFIRM) {
		if (event.signal == KeyboardInput::KEY_W || event.signal == KeyboardInput::KEY_S || event.signal == KeyboardInput::KEY_A || event.signal == KeyboardInput::KEY_D) m_staticAssetPanel.yesSelected = !m_staticAssetPanel.yesSelected;
		else if (event.signal == KeyboardInput::KEY_E || event.signal == KeyboardInput::KEY_ENTER) { if (m_staticAssetPanel.yesSelected) beginStaticParticleAssetJob(); else closeStaticParticleAssetPanel(); }
		else if (event.signal == KeyboardInput::KEY_Q || event.signal == KeyboardInput::KEY_ESCAPE) closeStaticParticleAssetPanel();
		glutPostRedisplay(); return true;
	}

	// --- WORKING --- //
	if (m_staticAssetPanel.mode == Mode::WORKING) return true;

	// --- FAILED --- //
	if (event.signal == KeyboardInput::KEY_E || event.signal == KeyboardInput::KEY_ENTER || event.signal == KeyboardInput::KEY_Q || event.signal == KeyboardInput::KEY_ESCAPE) closeStaticParticleAssetPanel();
	
	return true;
}

bool EuclidEngine::isStaticParticleAssetModalActive() const {
	return m_staticAssetPanel.mode != ViewPort::ObjExportPanelMode::HIDDEN;
}

void EuclidEngine::consumeWorkspaceHostRequest() {
	using GridRequest = Grid2DWorkspace::HostRequestType;
	const Grid2DWorkspace::HostRequest gridRequest =
		m_tesseract.takeGrid2DHostRequest();

	if (gridRequest.type == GridRequest::RefreshOutputCatalog) {
		std::vector<vitru::StaticAssetCatalogEntry> catalog =
			vitru::enumerateStaticParticleAssets(
				m_inputsRoot,
				m_outputRoot / "STATIC_PARTICLES");

		catalog.erase(
			std::remove_if(
				catalog.begin(), catalog.end(),
				[](const vitru::StaticAssetCatalogEntry& entry) {
					return entry.source != "OUTPUT";
				}),
			catalog.end());

		m_tesseract.replaceGrid2DOutputCatalog(std::move(catalog));
	}
	else if (gridRequest.type == GridRequest::LoadTarget) {
		const std::string targetName = gridRequest.target.displayName;
		vitru::StaticParticleAsset loadedAsset;
		vitru::StaticAssetOperationReport report;

		if (!gridRequest.target.valid ||
			!vitru::loadStaticParticleBundle(
				gridRequest.target.manifestPath,
				loadedAsset,
				report,
				nullptr,
				{})) {
			m_tesseract.completeGrid2DTargetLoad(
				false,
				false,
				vitru::INVALID_ASSET_ID,
				targetName,
				"TARGET LOAD FAILED: " + targetName +
				" | CONFIGURATION LOCKED.");
		}
		else {
			vitru::AssetId assetId = vitru::INVALID_ASSET_ID;
			if (gridRequest.replaceAssetId != vitru::INVALID_ASSET_ID &&
				m_assetRepository.findStaticParticle(gridRequest.replaceAssetId)) {
				assetId = gridRequest.replaceAssetId;
				if (!m_assetRepository.replaceStaticParticle(
					assetId, std::move(loadedAsset))) {
					assetId = vitru::INVALID_ASSET_ID;
				}
			}
			else {
				assetId = m_assetRepository.addStaticParticle(std::move(loadedAsset));
			}

			vitru::StaticParticleAsset* active =
				m_assetRepository.findStaticParticle(assetId);

			const bool installed =
				active != nullptr &&
				m_assetRepository.setActiveStaticParticle(assetId) &&
				m_renderer != nullptr &&
				m_renderer->loadParticleStaticAsset(*active);

			if (!installed) {
				m_tesseract.completeGrid2DTargetLoad(
					false,
					false,
					vitru::INVALID_ASSET_ID,
					targetName,
					"TARGET LOAD FAILED: " + targetName +
					" | CONFIGURATION LOCKED.");
			}
			else {
				bool ready = !active->mesh.empty() &&
					active->mesh.uvs.size() == active->mesh.positions.size() &&
					!active->materials.empty();

				std::size_t materialIndex = 0;
				if (ready && !active->submeshes.empty()) {
					const std::size_t candidate = static_cast<std::size_t>(
						active->submeshes.front().materialIndex);
					if (candidate < active->materials.size()) materialIndex = candidate;
				}

				if (ready) {
					const std::string& textureId =
						active->materials[materialIndex].baseColorTextureId;
					const vitru::TextureResource* texture =
						textureId.empty() ? nullptr : active->findTexture(textureId);
					ready = texture != nullptr && texture->valid;
				}

				m_tesseract.completeGrid2DTargetLoad(
					true,
					ready,
					assetId,
					active->name.empty() ? targetName : active->name,
					ready
					? "TARGET LOADED: " + targetName +
						" | TEXTURE_MAP_2D CONFIGURATION READY."
					: "TARGET LOADED: " + targetName +
						" | UV/TEXTURE PREREQUISITES NOT READY; CONFIGURATION LOCKED.");
			}
		}
	}

	using Request = SingleParticleWorkspace::HostRequest;
	switch (m_tesseract.takeSingleParticleHostRequest()) {
	case Request::LoadStaticParticle: openStaticParticleLoadPanel(); break;
	case Request::SaveStaticParticle: openStaticParticleSaveConfirm(""); break;
	case Request::SaveStaticParticleAs: openStaticParticleSaveAsPanel(); break;
	case Request::ExportObj: exportCurrentMeshOBJ(); break;
	default: break;
	}
}

bool EuclidEngine::makeCurrentStaticParticleAsset(
	vitru::StaticParticleAsset& output) const {
	const auto attachVolume = [this](vitru::StaticParticleAsset& asset) {
		std::vector<float> samples;
		if (!m_tesseract.exportSingleParticleVolume(samples)) return false;
		const int3& size = m_tesseract.singleParticleVolumeSize();
		asset.volumetricSource.available = true;
		asset.volumetricSource.file = "volume/base_volume.f32";
		asset.volumetricSource.format = "FLOAT32_SDF";
		asset.volumetricSource.dimensions = {
			static_cast<std::uint32_t>(size.x),
			static_cast<std::uint32_t>(size.y),
			static_cast<std::uint32_t>(size.z)
		};
		asset.volumetricSource.isoValue = 0.0f;
		asset.volumetricSource.samples = std::move(samples);
		return true;
	};

	MarchingCubes* marchingCubes = m_tesseract.singleParticleMarchingCubes();
	if (marchingCubes && marchingCubes->hasTriangleData() &&
		!marchingCubes->getCanonicalMesh().empty()) {
		output = vitru::StaticParticleAsset{};
		output.name = "Static Particle";
		output.mesh = marchingCubes->getCanonicalMesh();
		output.materials.emplace_back();
		output.source.kind = "NATIVE_SP_MCAD";
		output.anchor.particleIndex = 0u;
		output.anchor.pivotMode = vitru::ParticlePivotMode::GroundCenter;
		output.anchor.fitMode = vitru::ParticleFitMode::CollisionSafe;
		output.collision.shape = vitru::CollisionProxy::Shape::Sphere;
		output.collision.radius = m_tesseract.singleParticleRadius();
		output.refreshDerivedData();
		return attachVolume(output);
	}

	const vitru::StaticParticleAsset* active =
		m_assetRepository.activeStaticParticle();
	if (!active) return false;
	output = *active;
	if (m_tesseract.singleParticleHasCommittedGeometry())
		return attachVolume(output);
	return true;
}

void EuclidEngine::exportCurrentMeshOBJ() {
	if (isAnyHostModalActive()) return;

	m_objExportPanel = ViewPort::ObjExportPanelData{};
	m_objExportPanel.mode = ViewPort::ObjExportPanelMode::CONFIRM;
	m_objExportPanel.titleText = "VITRUGEN OBJ EXPORT";
	m_objExportPanel.confirmText = "Confirm Export .OBJ?";
	m_objExportPanel.yesSelected = true;
	m_objExportPanel.statusText = "Awaiting confirmation";
	m_objExportStage = ObjExportStage::NONE;
	glutDetachMenu(GLUT_RIGHT_BUTTON);
	glutPostRedisplay();
}

void EuclidEngine::appendObjExportLog(const string& line) {
	m_objExportPanel.logLines.push_back(line);
	if (m_objExportPanel.logLines.size() > 64)
		m_objExportPanel.logLines.erase(m_objExportPanel.logLines.begin());
}

void EuclidEngine::failObjExportJob(const string& reason) {
	appendObjExportLog("[EuclidEngine] ERROR: " + reason);
	m_objExportPanel.mode = ViewPort::ObjExportPanelMode::FAILED;
	m_objExportPanel.statusText = reason;
	m_objExportStage = ObjExportStage::NONE;
	m_objExportFutureActive = false;
	glutPostRedisplay();
}

void EuclidEngine::closeObjExportPanel() {
	if (m_objExportFutureActive) return;
	m_objExportPanel = ViewPort::ObjExportPanelData{};
	m_objExportStage = ObjExportStage::NONE;
	rebuildMenus();
	glutPostRedisplay();
}

void EuclidEngine::beginObjExportJob() {
	m_objExportPanel.mode = ViewPort::ObjExportPanelMode::WORKING;
	m_objExportPanel.titleText = "VITRUGEN OBJ EXPORT PIPELINE";
	m_objExportPanel.progressPercent = 5;
	m_objExportPanel.spinnerFrame = 0;
	m_objExportPanel.logLines.clear();
	m_objExportPanel.statusText = "Preparing Marching Cubes mesh";
	m_objExportStage = ObjExportStage::PREPARE_MESH;
	const int now = glutGet(GLUT_ELAPSED_TIME);
	m_objExportNextStepMs = now + 120;
	m_objExportLastSpinnerMs = now;
	glutPostRedisplay();
}

bool EuclidEngine::handleObjExportModalKeyboard(
	const KeyboardInput::KeyEvent& event) {
	if (!isObjExportModalActive()) return false;
	using Mode = ViewPort::ObjExportPanelMode;

	if (m_objExportPanel.mode == Mode::CONFIRM) {
		switch (event.signal) {
		case KeyboardInput::KEY_W:
		case KeyboardInput::KEY_S:
		case KeyboardInput::KEY_A:
		case KeyboardInput::KEY_D:
			m_objExportPanel.yesSelected = !m_objExportPanel.yesSelected;
			break;
		case KeyboardInput::KEY_1:
			m_objExportPanel.yesSelected = true;
			break;
		case KeyboardInput::KEY_2:
			m_objExportPanel.yesSelected = false;
			break;
		case KeyboardInput::KEY_E:
		case KeyboardInput::KEY_ENTER:
			if (m_objExportPanel.yesSelected) beginObjExportJob();
			else closeObjExportPanel();
			break;
		case KeyboardInput::KEY_Q:
		case KeyboardInput::KEY_ESCAPE:
			closeObjExportPanel();
			break;
		default:
			break;
		}
		return true;
	}

	if (m_objExportPanel.mode == Mode::WORKING) return true;
	if (event.signal == KeyboardInput::KEY_E ||
		event.signal == KeyboardInput::KEY_ENTER ||
		event.signal == KeyboardInput::KEY_Q ||
		event.signal == KeyboardInput::KEY_ESCAPE)
		closeObjExportPanel();
	return true;
}

void EuclidEngine::advanceObjExportJob() {
	using namespace std::chrono;
	const int now = glutGet(GLUT_ELAPSED_TIME);

	if (m_objExportPanel.mode == ViewPort::ObjExportPanelMode::COMPLETE) {
		if (now >= m_objExportCompleteUntilMs) closeObjExportPanel();
		return;
	}
	if (!isObjExportWorking()) return;

	if (now - m_objExportLastSpinnerMs >= 100) {
		m_objExportPanel.spinnerFrame =
			(m_objExportPanel.spinnerFrame + 1) % 4;
		m_objExportLastSpinnerMs = now;
	}

	MarchingCubes* marchingCubes =
		m_tesseract.singleParticleMarchingCubes();

	if (m_objExportStage == ObjExportStage::WAIT_FOR_FILE_WRITE) {
		if (!m_objExportFutureActive || !m_objExportFuture.valid()) {
			failObjExportJob("OBJ writer task became invalid.");
			return;
		}
		if (m_objExportFuture.wait_for(milliseconds(0)) !=
			future_status::ready) return;
		const bool succeeded = m_objExportFuture.get();
		m_objExportFutureActive = false;
		if (!succeeded || !marchingCubes) {
			failObjExportJob("MarchingCubes::exportOBJ() failed.");
			return;
		}

		char line[512];
		snprintf(line, sizeof(line),
			"[MarchingCubes3D] exportOBJ success: '%s' vertices=%zu triangles=%zu normals=YES indexed=YES",
			m_objExportPath.c_str(),
			marchingCubes->getCanonicalMesh().positions.size(),
			marchingCubes->getCanonicalMesh().triangleCount());
		appendObjExportLog(line);
		const vitru::MeshProcessingReport& report =
			marchingCubes->getMeshProcessingReport();
		snprintf(line, sizeof(line),
			"[MeshProcessor] rawV=%zu rawT=%zu finalV=%zu finalT=%zu removed=%zu welded=%zu radius=%.6f",
			report.rawVertexCount, report.rawTriangleCount,
			report.finalVertexCount, report.finalTriangleCount,
			report.removedNonFiniteTriangles +
			report.removedDegenerateTriangles +
			report.removedDuplicateTriangles,
			report.weldedVertexInstances, report.bounds.radius);
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 70;
		m_objExportPanel.statusText = "OBJ file written";
		m_objExportStage = ObjExportStage::REPORT_FILE_WRITTEN;
		m_objExportNextStepMs = now + 140;
		return;
	}

	if (now < m_objExportNextStepMs) return;
	char line[512];

	switch (m_objExportStage) {
	case ObjExportStage::PREPARE_MESH:
		appendObjExportLog("[EuclidEngine] Marching Cubes initialized.");
		m_objExportPanel.progressPercent = 10;
		m_objExportPanel.statusText = "Extracting Marching Cubes mesh";
		if (!m_tesseract.prepareSingleParticleMarchingCubesExport()) {
			failObjExportJob("No valid Marching Cubes triangle mesh.");
			return;
		}
		marchingCubes = m_tesseract.singleParticleMarchingCubes();
		m_objExportStage = ObjExportStage::REPORT_CLASSIFICATION;
		break;

	case ObjExportStage::REPORT_CLASSIFICATION:
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		snprintf(line, sizeof(line),
			"[MarchingCubes3D] classifyOnly: iso=%.4f, activeVoxels=%u, totalVerts=%u",
			0.0f, marchingCubes->getActiveVoxelCount(),
			marchingCubes->getTotalVertexCount());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 20;
		m_objExportPanel.statusText = "Voxel classification complete";
		m_objExportStage = ObjExportStage::REPORT_MC_VBO;
		break;

	case ObjExportStage::REPORT_MC_VBO:
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		snprintf(line, sizeof(line),
			"[MarchingCubes3D] Created mesh VBOs: verts=%u, bytes=%zu each",
			marchingCubes->getAllocatedMeshVertexCount(),
			marchingCubes->getMeshVBOBytesEach());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 25;
		m_objExportPanel.statusText = "Marching Cubes VBO ready";
		m_objExportStage = ObjExportStage::REPORT_BOUNDS;
		break;

	case ObjExportStage::REPORT_BOUNDS:
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		appendObjExportLog("[MarchingCubes3D] mesh bounds:");
		if (marchingCubes->hasMeshBounds()) {
			const float3 minValue = marchingCubes->getMeshMin();
			const float3 maxValue = marchingCubes->getMeshMax();
			const float3 centerValue = marchingCubes->getMeshCenter();
			snprintf(line, sizeof(line), "  min    = %.4f %.4f %.4f",
				minValue.x, minValue.y, minValue.z); appendObjExportLog(line);
			snprintf(line, sizeof(line), "  max    = %.4f %.4f %.4f",
				maxValue.x, maxValue.y, maxValue.z); appendObjExportLog(line);
			snprintf(line, sizeof(line), "  center = %.4f %.4f %.4f",
				centerValue.x, centerValue.y, centerValue.z); appendObjExportLog(line);
		}
		else appendObjExportLog("  bounds unavailable");
		m_objExportPanel.progressPercent = 30;
		m_objExportPanel.statusText = "Mesh bounds calculated";
		m_objExportStage = ObjExportStage::REPORT_EXTRACTION;
		break;

	case ObjExportStage::REPORT_EXTRACTION:
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		snprintf(line, sizeof(line),
			"[MarchingCubes3D] extract: activeVoxels=%u, totalVerts=%u, triangles=%u",
			marchingCubes->getActiveVoxelCount(),
			marchingCubes->getTotalVertexCount(),
			marchingCubes->getGeneratedTriangleCount());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 40;
		m_objExportPanel.statusText = "Triangle extraction complete";
		m_objExportStage = ObjExportStage::REPORT_ENGINE_EXTRACTION;
		break;

	case ObjExportStage::REPORT_ENGINE_EXTRACTION:
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		snprintf(line, sizeof(line),
			"[EuclidEngine] MC extract complete: verts=%u tris=%u valid=YES",
			marchingCubes->getTotalVertexCount(),
			marchingCubes->getGeneratedTriangleCount());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 50;
		m_objExportPanel.statusText = "Preparing file writer";
		m_objExportStage = ObjExportStage::BEGIN_FILE_WRITE;
		break;

	case ObjExportStage::BEGIN_FILE_WRITE: {
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		const fs::path outputPath(m_objExportPath);
		std::error_code error;
		if (!outputPath.parent_path().empty())
			fs::create_directories(outputPath.parent_path(), error);
		if (error) { failObjExportJob("OBJ output directory could not be created."); return; }
		m_objExportPanel.progressPercent = 60;
		m_objExportPanel.statusText = "Writing vertices, normals and faces";
		const string path = m_objExportPath;
		m_objExportFuture = async(launch::async,
			[marchingCubes, path]() {
				return marchingCubes->exportOBJ(path.c_str(), true);
			});
		m_objExportFutureActive = true;
		m_objExportStage = ObjExportStage::WAIT_FOR_FILE_WRITE;
		return;
	}

	case ObjExportStage::REPORT_FILE_WRITTEN:
		snprintf(line, sizeof(line), "[EuclidEngine] Exported OBJ: %s",
			m_objExportPath.c_str());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 75;
		m_objExportPanel.statusText = "Preparing OBJ mesh reload";
		m_objExportStage = ObjExportStage::BEGIN_MESH_RELOAD;
		break;

	case ObjExportStage::BEGIN_MESH_RELOAD:
		m_objExportPanel.progressPercent = 80;
		m_objExportPanel.statusText = "Uploading exported mesh";
		m_objExportStage = ObjExportStage::RELOAD_MESH;
		break;

	case ObjExportStage::RELOAD_MESH: {
		if (!m_renderer) { failObjExportJob("Renderer unavailable."); return; }
		if (!marchingCubes) { failObjExportJob("Marching Cubes unavailable."); return; }
		m_renderer->clearParticleMeshOBJ();
		if (!m_renderer->loadParticleMeshOBJ(m_objExportPath.c_str())) {
			failObjExportJob("Generated OBJ could not be reloaded.");
			return;
		}
		snprintf(line, sizeof(line),
			"[EuclidRenderer] OBJ mesh uploaded: vbo=%u vertices=%d bytes=%zu",
			static_cast<unsigned int>(m_renderer->getParticleMeshVBO()),
			static_cast<int>(m_renderer->getParticleMeshVertexCount()),
			m_renderer->getParticleMeshBufferBytes());
		appendObjExportLog(line);

		if (marchingCubes->getCanonicalMesh().bounds.valid) {
			const vitru::MeshBounds& exported =
				marchingCubes->getCanonicalMesh().bounds;
			const glm::vec3 loadedMin = m_renderer->getParticleMeshMin();
			const glm::vec3 loadedMax = m_renderer->getParticleMeshMax();
			const glm::vec3 loadedCenter = m_renderer->getParticleMeshCenter();
			const float loadedExtent = m_renderer->getParticleMeshMaxExtent();
			float delta = fabsf(loadedExtent - exported.maxAxisExtent);
			delta = (std::max)(delta, fabsf(loadedMin.x - exported.min.x));
			delta = (std::max)(delta, fabsf(loadedMin.y - exported.min.y));
			delta = (std::max)(delta, fabsf(loadedMin.z - exported.min.z));
			delta = (std::max)(delta, fabsf(loadedMax.x - exported.max.x));
			delta = (std::max)(delta, fabsf(loadedMax.y - exported.max.y));
			delta = (std::max)(delta, fabsf(loadedMax.z - exported.max.z));
			delta = (std::max)(delta, fabsf(loadedCenter.x - exported.center.x));
			delta = (std::max)(delta, fabsf(loadedCenter.y - exported.center.y));
			delta = (std::max)(delta, fabsf(loadedCenter.z - exported.center.z));
			const size_t loadedTriangles = static_cast<size_t>(
				m_renderer->getParticleMeshVertexCount()) / 3u;
			const bool pass = loadedTriangles ==
				marchingCubes->getCanonicalMesh().triangleCount() &&
				delta <= 1.0e-5f;
			snprintf(line, sizeof(line),
				"[OBJ Roundtrip] triangles=%zu/%zu maxBoundsDelta=%.8f extent=%.6f/%.6f %s",
				loadedTriangles,
				marchingCubes->getCanonicalMesh().triangleCount(),
				delta, loadedExtent, exported.maxAxisExtent,
				pass ? "PASS" : "MISMATCH");
			appendObjExportLog(line);
		}
		m_objExportPanel.statusText = "GPU mesh upload complete";
		m_objExportStage = ObjExportStage::REPORT_MESH_LOADED;
		break;
	}

	case ObjExportStage::REPORT_MESH_LOADED:
		snprintf(line, sizeof(line),
			"[EuclidRenderer] Loaded particle mesh OBJ: %s vertices=%d triangles=%d maxExtent=%.6f",
			m_objExportPath.c_str(),
			static_cast<int>(m_renderer->getParticleMeshVertexCount()),
			static_cast<int>(m_renderer->getParticleMeshVertexCount() / 3),
			m_renderer->getParticleMeshMaxExtent());
		appendObjExportLog(line);
		m_objExportPanel.progressPercent = 90;
		m_objExportPanel.statusText = "Mesh reload verified";
		m_objExportStage = ObjExportStage::ACTIVATE_MESH_MODE;
		break;

	case ObjExportStage::ACTIVATE_MESH_MODE:
		m_tesseract.activateSingleParticleExportedMeshRender();
		appendObjExportLog(
			"[EuclidEngine] SINGLE_PARTICLE render mode automatically set to MESH.");
		m_objExportPanel.progressPercent = 99;
		m_objExportPanel.statusText = "Finalizing export";
		m_objExportStage = ObjExportStage::FINISH;
		break;

	case ObjExportStage::FINISH:
		m_objExportPanel.progressPercent = 100;
		m_objExportPanel.mode = ViewPort::ObjExportPanelMode::COMPLETE;
		m_objExportPanel.statusText = "Export complete";
		m_objExportCompleteUntilMs = now + 1400;
		m_objExportStage = ObjExportStage::NONE;
		glutPostRedisplay();
		return;

	default:
	case ObjExportStage::NONE:
	case ObjExportStage::WAIT_FOR_FILE_WRITE:
		return;
	}

	m_objExportNextStepMs = now + 140;
}

bool EuclidEngine::handleHostModalKeyboard(unsigned char rawKey) {
	if (m_hostModalMode == HostModalMode::Hidden) return false;
	if (m_hostModalMode == HostModalMode::SaveName) {
		const TextEntryAction action = m_hostTextEntry.handleRawKey(rawKey);
		if (action == TextEntryAction::Committed) {
			m_pendingStaticAssetName = m_hostTextEntry.getNormalizedText();
			m_hostModalYesSelected = true;
			m_hostModalMode = HostModalMode::SaveConfirm;
			m_hostModalMessage = "Save named asset: " +
				m_pendingStaticAssetName + " ?";
		}
		else if (action == TextEntryAction::Cancelled) closeHostModal();
		return true;
	}
	const unsigned char key = static_cast<unsigned char>(std::tolower(rawKey));
	if (m_hostModalMode == HostModalMode::LoadSelect) {
		const int count = static_cast<int>(m_staticAssetCatalog.size());
		if (count > 0 && key == 'w')
			m_hostModalSelectedIndex = (m_hostModalSelectedIndex + count - 1) % count;
		else if (count > 0 && key == 's')
			m_hostModalSelectedIndex = (m_hostModalSelectedIndex + 1) % count;
		else if (count > 0 && (key == 'e' || rawKey == 13)) beginStaticParticleAssetJob();
		else if (key == 'q' || rawKey == 27) closeHostModal();
		return true;
	}
	if (m_hostModalMode == HostModalMode::SaveConfirm) {
		if (key == 'w' || key == 's' || key == 'a' || key == 'd')
			m_hostModalYesSelected = !m_hostModalYesSelected;
		else if (key == 'e' || rawKey == 13) {
			if (m_hostModalYesSelected) beginStaticParticleAssetJob();
			else closeHostModal();
		}
		else if (key == 'q' || rawKey == 27) closeHostModal();
		return true;
	}
	if (m_hostModalMode == HostModalMode::Working) return true;
	if (key == 'e' || key == 'q' || rawKey == 13 || rawKey == 27)
		closeHostModal();
	return true;
}

WorkspacePresentation EuclidEngine::buildHostModalPresentation() const {
	WorkspacePresentation p;
	const auto hostRow = [](const std::string& label,
		const std::string& value, bool selected) {
			WorkspacePanelRow row;
			row.label = label;
			row.value = value;
			row.selectable = true;
			row.selected = selected;
			return row;
	};
	p.panelVisible = true;
	p.workspaceName = "VITRUGEN HOST";
	p.layerLabel = "STATIC_PARTICLE_ASSET PERSISTENCE";
	p.statusLine = m_hostModalMessage;
	WorkspacePanelSection section;
	if (m_hostModalMode == HostModalMode::LoadSelect) {
		section.heading = "Select Static Particle:";
		for (std::size_t i = 0; i < m_staticAssetCatalog.size(); ++i)
			section.rows.push_back(hostRow(
				m_staticAssetCatalog[i].displayName,
				m_staticAssetCatalog[i].source,
				static_cast<int>(i) == m_hostModalSelectedIndex));
		p.footerLine1 = "W/S: Select    E: Load";
		p.footerLine2 = "Q / ESC: Cancel";
	}
	else if (m_hostModalMode == HostModalMode::SaveName) {
		section.heading = m_hostTextEntry.getPrompt();
		section.rows.push_back(hostRow("NAME", m_hostTextEntry.getBuffer(), true));
		p.footerLine1 = "Type asset name    ENTER: Continue";
		p.footerLine2 = "ESC: Cancel";
	}
	else if (m_hostModalMode == HostModalMode::SaveConfirm) {
		section.heading = "Confirm Save:";
		section.rows.push_back(hostRow("YES", "", m_hostModalYesSelected));
		section.rows.push_back(hostRow("NO", "", !m_hostModalYesSelected));
		p.footerLine1 = "W/S or A/D: Choose    E: Confirm";
		p.footerLine2 = "Q / ESC: Cancel";
	}
	else {
		section.heading = m_hostModalMode == HostModalMode::Working
			? "Operation In Progress:" : "Operation Result:";
		section.rows.push_back(hostRow(m_hostModalMessage, "", false));
		p.footerLine1 = m_hostModalMode == HostModalMode::Working
			? "Please wait..." : "E / Q / ESC: Close";
		p.statusTone = m_hostModalMode == HostModalMode::Failed
			? WorkspaceStatusTone::Warning : WorkspaceStatusTone::Ready;
	}
	p.sections.push_back(section);
	return p;
}

void EuclidEngine::closeHostModal() {
	if (m_staticAssetFutureActive) return;
	m_hostModalMode = HostModalMode::Hidden;
	m_hostModalMessage.clear();
	m_staticAssetCatalog.clear();
	m_staticAssetJobKind = StaticAssetJobKind::None;
	m_pendingStaticAssetName.clear();
	m_hostTextEntry.reset();
}

bool EuclidEngine::isObjExportModalActive() const {

	return m_objExportPanel.mode !=
		ViewPort::ObjExportPanelMode::HIDDEN;
}

bool EuclidEngine::isObjExportWorking() const {
	return m_objExportPanel.mode ==
		ViewPort::ObjExportPanelMode::WORKING;
}

bool EuclidEngine::isAnyHostModalActive() const {
	return isObjExportModalActive() ||
		isStaticParticleAssetModalActive() ||
		m_hostModalMode != HostModalMode::Hidden;
}
