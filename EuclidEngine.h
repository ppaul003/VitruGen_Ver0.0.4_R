#ifndef __EUCLID_ENGINE_H__
#define __EUCLID_ENGINE_H__

#ifdef _WIN32
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/wglew.h>
#include <GL/freeglut.h>

#include <helper_functions.h>
#include <helper_cuda.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#include <future>
#include <filesystem>
#include <string>

#include "Interactions.h"
#include "Camera.h"
#include "ViewPort.h"
#include "TheArbiter.h"
#include "TheTesseract.h"

#include "marchingCubes.h"
#include "renderer_Euclid.h"
#include "StaticParticleAssetIO.h"
#include "ProjectAssetRepository.h"
#include "TextEntry.h"

class EuclidEngine {
public:
	EuclidEngine();
	~EuclidEngine();

	// --- APPLICATION LIFECYCLE / PLATFORM SETUP ---
	bool init(int argc, char** argv);
	void initGL(int* argc, char** argv);

	void initMenus();
	void rebuildMenus();

	void initRenderer();
	bool initWorkspaceHost();

	WorkspaceFrameContext
		buildWorkspaceFrameContext(float deltaTime) const;

	void run();
	void shutdown();
	void computeFPS();
	void requestExit();

private:
	static EuclidEngine* s_instance;

	// --- ENGINE-LOCAL TYPES ---
	enum class StaticAssetJobKind { 
		None = 0, 
		Save, 
		Load 
	};

	enum class HostModalMode {
		Hidden = 0,
		LoadSelect,
		SaveName,
		SaveConfirm,
		Working,
		Complete,
		Failed
	};

	struct StaticAssetAsyncResult {
		bool success = false;
		vitru::StaticParticleAsset asset;
		vitru::StaticAssetOperationReport report;
	};

	enum class ObjExportStage {
		NONE = 0,

		PREPARE_MESH,
		REPORT_CLASSIFICATION,
		REPORT_MC_VBO,
		REPORT_BOUNDS,
		REPORT_EXTRACTION,
		REPORT_ENGINE_EXTRACTION,

		BEGIN_FILE_WRITE,
		WAIT_FOR_FILE_WRITE,
		REPORT_FILE_WRITTEN,

		BEGIN_MESH_RELOAD,
		RELOAD_MESH,
		REPORT_MESH_LOADED,

		ACTIVATE_MESH_MODE,
		FINISH
	};

	// --- GLUT CALLBACK BRIDGE ---
	static void sDisplay();
	static void sReshape(int w, int h);
	static void sMouse(int button, int state, int x, int y);
	static void sMotion(int x, int y);
	static void sPassiveMotion(int x, int y);
	static void sMainMenu(int value);
	static void sMenuStatus(int status, int x, int y);
	static void sKeyboard(unsigned char key, int x, int y);
	static void sIdle();
	static void sClose();

	// --- RUNTIME EVENT HANDLERS ---
	void onDisplay();
	void onReshape(int w, int h);
	void onMouse(int button, int state, int x, int y);
	void onMotion(int x, int y);
	void onPassiveMotion(int x, int y);
	void onKeyboard(unsigned char key, int x, int y);
	void onIdle();
	void onClose();

	void consumeWorkspaceHostRequest();
	bool handleHostModalKeyboard(unsigned char rawKey);
	WorkspacePresentation buildHostModalPresentation() const;
	bool makeCurrentStaticParticleAsset(vitru::StaticParticleAsset& output) const;
	void beginStaticParticleLoad();
	void beginStaticParticleSave(bool saveAs);
	void beginStaticParticleJob();
	void advanceStaticParticleJob();
	void exportCurrentSingleParticleObj();
	void closeHostModal();

	// HOST CONFIGURATION
	static constexpr uint kWidth = 1920;
	static constexpr uint kHeight = 1080;

	// HOST MENU COMMANDS
	static constexpr int MENU_NOP = -1;
	static constexpr int MENU_QUIT = 27;
	static constexpr int MENU_WORKSPACE_COMMAND_BASE = 10000;

	// TRANSITIONAL ASSET PERSISTENCE COMMANDS
	static constexpr int MENU_EXPORT_OBJ = 1001;
	static constexpr int MENU_SAVE_STATIC_PARTICLE = 1004;
	static constexpr int MENU_SAVE_STATIC_PARTICLE_AS = 1005;

	// --- APPLICATION CORE ---
	TheArbiter m_arbiter;
	Tesseract m_tesseract;
	ViewPort m_viewport;
	CameraProcessor m_camera;

	// --- INPUT TRANSLATION ---
	MouseInput m_mouse;
	KeyboardInput m_keyboard;

	// --- SHARED HOST SERVICES
	EuclidRenderer* m_renderer = nullptr;

	// --- RUNTIME STATE / GLUT SUPPORT ---
	bool m_displayEnabled = true;
	bool m_exiting = false;
	bool m_cleaned = false;

	int m_menuId = 0;
	std::vector<int> m_workspaceMenuCommands;

	// --- SAVE/LODAD/WRITE SP ASSET MEMBERS ---
	std::string m_objExportPath = "SINGLE_PARTICLE_DATA/p0.obj";

	StaticAssetJobKind m_staticAssetJobKind = StaticAssetJobKind::None;
	std::future<StaticAssetAsyncResult> m_staticAssetFuture;
	bool m_staticAssetFutureActive = false;
	int m_staticAssetLastSpinnerMs = 0;
	std::string m_pendingStaticAssetName;
	std::vector<vitru::StaticAssetCatalogEntry> m_staticAssetCatalog;

	std::future<bool> m_objExportFuture;
	ObjExportStage m_objExportStage = ObjExportStage::NONE;

	bool m_objExportFutureActive = false;
	int m_objExportNextStepMs = 0;
	int m_objExportLastSpinnerMs = 0;
	int m_objExportCompleteUntilMs = 0;

	bool m_textureMapSaveAsAwaitingSurfaceName = false;
	std::string m_textureMapSaveAsSurfaceTargetName;

	HostModalMode m_hostModalMode = HostModalMode::Hidden;
	int m_hostModalSelectedIndex = 0;
	bool m_hostModalYesSelected = true;
	std::string m_hostModalMessage;
	TextEntrySession m_hostTextEntry;

	vitru::ProjectAssetRepository m_assetRepository;

	std::filesystem::path m_inputsRoot = "INPUTS";
	std::filesystem::path m_outputRoot = "OUTPUT";
	std::filesystem::path m_workspaceObj = "SINGLE_PARTICLE_DATA/p0.obj";
	// --- SAVE/LODAD/WRITE SP ASSET MEMBERS ---
};

#endif
