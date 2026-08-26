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

	// ---------------------------------------------------------
	// Render generic workspace presentation.
	// ---------------------------------------------------------
	m_viewport.drawOverlay(m_hostModalMode == HostModalMode::Hidden
		? m_tesseract.presentation()
		: buildHostModalPresentation()
	);

	glutSwapBuffers();
}

void EuclidEngine::onMouse(int button, int state, int x, int y) {
	// ---------------------------------------------------------
	// Layer 0 / domain transitions are keyboard-driven.
	//
	// Match GOLD behavior:
	//     no orbit
	//     no wheel zoom
	//     no workspace mouse interaction
	// ---------------------------------------------------------
	if (m_arbiter.isGlobalShell() || m_tesseract.domainTransitionActive())
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
	if (m_arbiter.isGlobalShell() || 
		m_tesseract.domainTransitionActive()) return;

	const TheArbiter::ArbiterResult result =
		m_arbiter.routePointerMove(x, y);

	if (result.hasWorkspaceInput) {

		m_tesseract.handleInput(result.workspaceInput);
	}

	m_mouse.onPassiveMotion(x, y);
}

void EuclidEngine::onKeyboard(unsigned char key, int x, int y) {
	if (handleHostModalKeyboard(key)) {
		glutPostRedisplay();
		return;
	}

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

void EuclidEngine::consumeWorkspaceHostRequest() {
	using Request = SingleParticleWorkspace::HostRequest;
	switch (m_tesseract.takeSingleParticleHostRequest()) {
	case Request::LoadStaticParticle: beginStaticParticleLoad(); break;
	case Request::SaveStaticParticle: beginStaticParticleSave(false); break;
	case Request::SaveStaticParticleAs: beginStaticParticleSave(true); break;
	case Request::ExportObj: exportCurrentSingleParticleObj(); break;
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

void EuclidEngine::beginStaticParticleLoad() {
	if (m_hostModalMode != HostModalMode::Hidden || m_staticAssetFutureActive)
		return;
	m_staticAssetCatalog = vitru::enumerateStaticParticleAssets(
		m_inputsRoot, m_outputRoot / "STATIC_PARTICLES");
	m_staticAssetCatalog.erase(
		std::remove_if(m_staticAssetCatalog.begin(), m_staticAssetCatalog.end(),
			[](const vitru::StaticAssetCatalogEntry& entry) { return !entry.valid; }),
		m_staticAssetCatalog.end());
	m_hostModalSelectedIndex = 0;
	m_staticAssetJobKind = StaticAssetJobKind::Load;
	m_hostModalMessage = m_staticAssetCatalog.empty()
		? "No valid StaticParticleAsset bundles were found."
		: "Select a StaticParticleAsset bundle.";
	m_hostModalMode = m_staticAssetCatalog.empty()
		? HostModalMode::Failed : HostModalMode::LoadSelect;
}

void EuclidEngine::beginStaticParticleSave(bool saveAs) {
	if (m_hostModalMode != HostModalMode::Hidden || m_staticAssetFutureActive)
		return;
	vitru::StaticParticleAsset asset;
	if (!makeCurrentStaticParticleAsset(asset)) {
		m_hostModalMode = HostModalMode::Failed;
		m_hostModalMessage = "No canonical mesh or active StaticParticleAsset.";
		return;
	}
	const vitru::StaticParticleAsset* active =
		m_assetRepository.activeStaticParticle();
	if (!saveAs && active && !active->name.empty()) {
		m_pendingStaticAssetName = active->name;
		m_staticAssetJobKind = StaticAssetJobKind::Save;
		m_hostModalYesSelected = true;
		m_hostModalMode = HostModalMode::SaveConfirm;
		m_hostModalMessage = "Save named asset: " + m_pendingStaticAssetName + " ?";
		return;
	}
	const std::string initial = active && !active->name.empty()
		? active->name : "Static Particle";
	m_hostTextEntry.beginAssetName("STATIC PARTICLE ASSET NAME", initial, 96u);
	m_staticAssetJobKind = StaticAssetJobKind::Save;
	m_hostModalMode = HostModalMode::SaveName;
	m_hostModalMessage = "Enter a portable asset name, then press ENTER.";
}

void EuclidEngine::beginStaticParticleJob() {
	if (m_staticAssetFutureActive) return;
	const StaticAssetJobKind kind = m_staticAssetJobKind;
	vitru::StaticParticleAsset source;
	fs::path manifest;
	if (kind == StaticAssetJobKind::Save) {
		if (!makeCurrentStaticParticleAsset(source)) {
			m_hostModalMode = HostModalMode::Failed;
			m_hostModalMessage = "Active asset validation failed.";
			return;
		}
	}
	else if (kind == StaticAssetJobKind::Load) {
		if (m_hostModalSelectedIndex < 0 ||
			m_hostModalSelectedIndex >= static_cast<int>(m_staticAssetCatalog.size()))
			return;
		manifest = m_staticAssetCatalog[
			static_cast<std::size_t>(m_hostModalSelectedIndex)].manifestPath;
	}
	else return;

	const fs::path outputRoot = m_outputRoot;
	const fs::path workspaceObj = m_workspaceObj;
	const std::string name = m_pendingStaticAssetName;
	m_staticAssetFuture = std::async(std::launch::async,
		[kind, source, manifest, outputRoot, workspaceObj, name]() mutable {
			StaticAssetAsyncResult result;
			if (kind == StaticAssetJobKind::Save) {
				result.success = vitru::saveStaticParticleBundle(
					source, outputRoot, name, workspaceObj, result.report);
				if (result.success) {
					vitru::StaticAssetOperationReport reopened;
					result.success = vitru::loadStaticParticleBundle(
						result.report.manifestPath, result.asset, reopened,
						nullptr, workspaceObj);
					result.report.warnings.insert(result.report.warnings.end(),
						reopened.warnings.begin(), reopened.warnings.end());
					result.report.errors.insert(result.report.errors.end(),
						reopened.errors.begin(), reopened.errors.end());
				}
			}
			else {
				result.success = vitru::loadStaticParticleBundle(
					manifest, result.asset, result.report, nullptr, workspaceObj);
			}
			return result;
		});
	m_staticAssetFutureActive = true;
	m_staticAssetLastSpinnerMs = glutGet(GLUT_ELAPSED_TIME);
	m_hostModalMode = HostModalMode::Working;
	m_hostModalMessage = kind == StaticAssetJobKind::Save
		? "Writing VSPA bundle..." : "Reading VSPA bundle...";
}

void EuclidEngine::advanceStaticParticleJob() {
	if (!m_staticAssetFutureActive || !m_staticAssetFuture.valid()) return;
	if (m_staticAssetFuture.wait_for(std::chrono::milliseconds(0)) !=
		std::future_status::ready) return;
	StaticAssetAsyncResult result = m_staticAssetFuture.get();
	m_staticAssetFutureActive = false;
	if (!result.success) {
		m_hostModalMode = HostModalMode::Failed;
		m_hostModalMessage = result.report.errors.empty()
			? "StaticParticleAsset operation failed during " + result.report.phase
			: result.report.errors.front();
		return;
	}

	const vitru::AssetId id = m_assetRepository.addStaticParticle(result.asset);
	m_assetRepository.setActiveStaticParticle(id);
	vitru::StaticParticleAsset* active = m_assetRepository.activeStaticParticle();
	if (!active || !m_renderer || !m_renderer->loadParticleStaticAsset(*active)) {
		m_hostModalMode = HostModalMode::Failed;
		m_hostModalMessage = "GPU asset upload failed.";
		return;
	}
	bool editableVolumeRestored = false;
	if (active->volumetricSource.available) {
		const int3 size = make_int3(
			static_cast<int>(active->volumetricSource.dimensions[0]),
			static_cast<int>(active->volumetricSource.dimensions[1]),
			static_cast<int>(active->volumetricSource.dimensions[2]));
		if (!m_tesseract.restoreSingleParticleVolume(
			active->volumetricSource.samples, size)) {
			m_hostModalMode = HostModalMode::Failed;
			m_hostModalMessage = "Native FLOAT32_SDF GPU restore failed.";
			return;
		}
		editableVolumeRestored = true;
	}
	m_tesseract.activateLoadedSingleParticleBase(editableVolumeRestored);
	m_hostModalMode = HostModalMode::Complete;
	m_hostModalMessage = editableVolumeRestored
		? "Geometry, materials, textures, p0 and native volume ready."
		: "Geometry, materials, textures and p0 ready.";
	rebuildMenus();
}

void EuclidEngine::exportCurrentSingleParticleObj() {
	vitru::StaticParticleAsset asset;
	if (!makeCurrentStaticParticleAsset(asset)) {
		m_hostModalMode = HostModalMode::Failed;
		m_hostModalMessage = "No canonical mesh is available for OBJ export.";
		return;
	}
	std::string error;
	const fs::path path = m_objExportPath;
	if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
	if (!vitru::writeStaticParticleObj(asset, path, "", &error)) {
		m_hostModalMode = HostModalMode::Failed;
		m_hostModalMessage = error.empty() ? "OBJ export failed." : error;
		return;
	}
	m_hostModalMode = HostModalMode::Complete;
	m_hostModalMessage = "OBJ written: " + path.generic_string();
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
		else if (count > 0 && (key == 'e' || rawKey == 13)) beginStaticParticleJob();
		else if (key == 'q' || rawKey == 27) closeHostModal();
		return true;
	}
	if (m_hostModalMode == HostModalMode::SaveConfirm) {
		if (key == 'w' || key == 's' || key == 'a' || key == 'd')
			m_hostModalYesSelected = !m_hostModalYesSelected;
		else if (key == 'e' || rawKey == 13) {
			if (m_hostModalYesSelected) beginStaticParticleJob();
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
