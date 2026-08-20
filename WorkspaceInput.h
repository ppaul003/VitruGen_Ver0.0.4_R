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
	ZoomOut
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
