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
#include "particleSystem.h"
#include "renderer_Euclid.h"
#include "StaticParticleAssetIO.h"
#include "ProjectAssetRepository.h"

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

	// --- CONSTANTS ---
	static constexpr uint kWidth = 1920;
	static constexpr uint kHeight = 1080;
	static constexpr uint kGridSize = 64;

	static constexpr uint kParticleSimCapacity = 16384;
	static constexpr uint kSingleParticleCapacity = 1;

	static constexpr int MENU_NOP = -1;
	static constexpr int MENU_EDIT_SCALE_WHOLE = '1';
	static constexpr int MENU_EDIT_SCALE_Z = '2';
	static constexpr int MENU_EDIT_SCALE_Y = '3';
	static constexpr int MENU_EDIT_SCALE_X = '4';
	static constexpr int MENU_RESET_OBJECT_SCALE = '0';
	static constexpr int MENU_EDIT_ROT_PITCH = '5';
	static constexpr int MENU_EDIT_ROT_YAW = '6';
	static constexpr int MENU_EDIT_ROT_ROLL = '7';
	static constexpr int MENU_RESET_OBJECT_ROTATION = '8';
	static constexpr int MENU_SELECT_WORKPLANE_PARTICLE = 'e';
	static constexpr int MENU_EDIT_PARTICLE_MESH = 'e';
	static constexpr int MENU_GO_BACK_SUBLAYER = 'q';
	static constexpr int MENU_QUIT = 27;
	static constexpr int MENU_EXPORT_OBJ = 1001;
	static constexpr int MENU_SAVE_STATIC_PARTICLE = 1004;
	static constexpr int MENU_SAVE_STATIC_PARTICLE_AS = 1005;

	// SUB_LAYER_3 Marching Cubes navigation.
	// These deliberately reuse the existing keyboard transition paths.
	static constexpr int MENU_MC_TO_SUB_LAYER_2 = 1002;
	static constexpr int MENU_MC_TO_SUB_LAYER_0 = 1003;
	// SUB_LAYER_2 assembly-node navigation.
	static constexpr int MENU_TO_NODE_PREVIEW = 1101;
	static constexpr int MENU_TO_NODE_EDIT_OBJECT = 1102;
	static constexpr int MENU_TO_NODE_OFFSET_OBJECT = 1103;
	static constexpr int MENU_TO_NODE_APPLY_BASE = 1104;
	static constexpr int MENU_RUN_MC_MODE = 1105;
	// Node 2 / Node 3 offset controls.
	// These remain placeholders during this checkpoint.
	static constexpr int MENU_OFFSET_Z_VECTOR = 1201;
	static constexpr int MENU_OFFSET_Y_VECTOR = 1202;
	static constexpr int MENU_OFFSET_X_VECTOR = 1203;
	static constexpr int MENU_RESET_OBJECT_OFFSET = 1204;
	// VOLUME_1 injection brush Boolean mode.
	static constexpr int MENU_TOGGLE_INJECTION_MODE = 1205;
	// Node 3 basis-rebase action.
	static constexpr int MENU_COMMIT_NEW_BASE_VECTOR = 1301;
	// Node_1 injection-brush local basis commit.
	static constexpr int MENU_COMMIT_BRUSH_BASE = 1302;
	static constexpr int MENU_SP_MIRROR_NONE = 1303;
	static constexpr int MENU_SP_MIRROR_ON = 1304;
	static constexpr int MENU_PRIMITIVE_BASE = 1400;
	static constexpr int MENU_PRIMITIVE_SPHERE = 1401;
	static constexpr int MENU_PRIMITIVE_TORUS = 1402;
	static constexpr int MENU_PRIMITIVE_BLOCK = 1403;
	static constexpr int MENU_PRIMITIVE_CYLINDER = 1404;
	static constexpr int MENU_PRIMITIVE_CONE = 1405;
	static constexpr int MENU_PRIMITIVE_CAPSULE = 1406;
	static constexpr int MENU_PRIMITIVE_WEDGE = 1407;
	static constexpr int MENU_PRIMITIVE_DELTA_WING = 1408;
	static constexpr int MENU_PRIMITIVE_FRUSTUM = 1409;
	// SUB_LAYER_0 selection/collision setup.
	static constexpr int MENU_SP_PRIMARY_ACTION = 2001;
	static constexpr int MENU_SP_COLLISION_SPHERE = 2002;
	static constexpr int MENU_SP_COLLISION_BLOCK = 2003;
	static constexpr int MENU_SP_COLLISION_CAPSULE = 2004;
	static constexpr int MENU_SP_COLLISION_CONE = 2005;
	static constexpr int MENU_SP_COLLISION_DEFORMABLE_SPHERE = 2006;
	static constexpr int MENU_SP_RENDERING_SETUP = 2007;
	static constexpr int MENU_SP_RETURN_TO_LAYER_2 = 2008;
	static constexpr int MENU_SP_LOAD_STATIC_PARTICLE = 2009;
	static constexpr int MENU_SP_SAVE_ACTIVE_PARTICLE = 2010;

	// SUB_LAYER_1 rendering setup.
	static constexpr int MENU_SP_RENDER_SOURCE_PARTICLE = 2101;
	static constexpr int MENU_SP_RENDER_SOURCE_MESH = 2102;
	static constexpr int MENU_SP_MESH_BOUND_DEFAULT = 2103;
	static constexpr int MENU_SP_MESH_BOUND_FILL = 2104;
	static constexpr int MENU_SP_DISPLAY_RENDER = 2105;
	static constexpr int MENU_SP_DISPLAY_RENDER_COLLISION = 2106;
	static constexpr int MENU_SP_DISPLAY_WIREFRAME = 2107;
	static constexpr int MENU_SP_RENDER_CAGE_ON = 2108;
	static constexpr int MENU_SP_RENDER_CAGE_OFF = 2109;
	static constexpr int MENU_SP_VOLUME_PREVIEW = 2110;
	static constexpr int MENU_SP_COLLISION_SETUP = 2111;
	// TEXTURE_MAP_2D Layer 3 context-menu parity. Row commands are
	// interpreted by the same Arbiter/workspace path as keyboard E.
	static constexpr int MENU_TM_RUNTIME_ROW_0 = 3000;
	static constexpr int MENU_TM_RUNTIME_ROW_1 = 3001;
	static constexpr int MENU_TM_RUNTIME_ROW_2 = 3002;
	static constexpr int MENU_TM_RUNTIME_ROW_3 = 3003;
	static constexpr int MENU_TM_RUNTIME_ROW_4 = 3004;
	static constexpr int MENU_TM_RUNTIME_ROW_5 = 3005;
	static constexpr int MENU_TM_RUNTIME_ROW_6 = 3006;
	static constexpr int MENU_TM_RUNTIME_TOGGLE_VIEW = 3010;
	static constexpr int MENU_TM_RUNTIME_TOGGLE_MESH = 3011;

	static constexpr float kMarchingCubesIsoValue = 0.0f;

	// --- APPLICATION CORE ---
	TheArbiter m_arbiter;
	Tesseract m_tesseract;
	ViewPort m_viewport;
	CameraProcessor m_camera;

	// --- INPUT ---
	MouseInput m_mouse;
	KeyboardInput m_keyboard;

	// --- SHARED PARTICLE SIMULATION / RENDER RESOURCES ---
	uint3 m_gridSizeDim{};
	EuclidRenderer* m_renderer = nullptr;

	// WORKSPACE RESOURCE OWNERSHIP BRANCH:
	//
	// SIMCAD_4D / PARTICLE_SIMULATION:
	ParticleSystem* m_particleSimSystem = nullptr;
	std::vector<float> m_particleSimRadii;

	uint m_particleSimCapacity = kParticleSimCapacity;
	uint m_particleSimActiveCount = kParticleSimCapacity;

	GLuint m_pbo = 0;
	GLuint m_tex = 0;
	
	struct cudaGraphicsResource* m_cudaPboResource = nullptr;
	//
	// --- RUNTIME STATE / GLUT SUPPORT ---
	bool m_displayEnabled = true;
	bool m_exiting = false;
	bool m_cleaned = false;
	int m_menuId = 0;
	int m_fpsCount = 0;
	int m_fpsLimit = 1;
	StopWatchInterface* m_timer = nullptr;
	
	// --- SINGLE_PARTICLE_MCAD SUB COMPONENT:
	// --- OBJ EXPORT JOB ---
	
	ObjExportStage m_objExportStage = ObjExportStage::NONE;
	//
	bool m_objExportFutureActive = false;
	int m_objExportNextStepMs = 0;
	int m_objExportLastSpinnerMs = 0;
	int m_objExportCompleteUntilMs = 0;
	//
	std::future<bool> m_objExportFuture;
	std::string m_objExportPath = "SINGLE_PARTICLE_DATA/p0.obj";

	StaticAssetJobKind m_staticAssetJobKind = StaticAssetJobKind::None;
	std::future<StaticAssetAsyncResult> m_staticAssetFuture;
	bool m_staticAssetFutureActive = false;
	int m_staticAssetLastSpinnerMs = 0;
	std::string m_pendingStaticAssetName;
	std::vector<vitru::StaticAssetCatalogEntry> m_staticAssetCatalog;
	bool m_textureMapSaveAsAwaitingSurfaceName = false;
	std::string m_textureMapSaveAsSurfaceTargetName;

	vitru::ProjectAssetRepository m_assetRepository;

	std::filesystem::path m_inputsRoot = "INPUTS";
	std::filesystem::path m_outputRoot = "OUTPUT";
	std::filesystem::path m_workspaceObj = "SINGLE_PARTICLE_DATA/p0.obj";
};

#endif
