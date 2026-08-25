#ifndef VITRUGEN_WORKSPACE_INPUT_H
#define VITRUGEN_WORKSPACE_INPUT_H

enum class WorkspaceInputAction {

	None = 0,

	Previous,
	Next,

	Decrease,
	Increase,

	Activate,
	Back,
	Toggle,

	PointerDown,
	PointerUp,
	PointerMove,

	ZoomIn,
	ZoomOut,

	// Generic numbered command slots used by workspaces that expose
	// keyboard-selectable transform or tool modes. The Arbiter owns
	// only the raw-key translation; the active cartridge supplies meaning.
	Option0,
	Option1,
	Option2,
	Option3,
	Option4,
	Option5,
	Option6,
	Option7,
	Option8

};

struct WorkspaceInputEvent {

	WorkspaceInputAction action =
		WorkspaceInputAction::None;

	int x = 0;
	int y = 0;

	int button = -1;
	int state = -1;
};

#endif
