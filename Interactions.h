#ifndef INTERACTIONS_H
#define INTERACTIONS_H

#include <GL/freeglut.h>

#include <cctype>
#include <cmath>

// -----------------------------------------------------------------------------
// KeyboardInput
// -----------------------------------------------------------------------------
// Unified keyboard input decoder for VitruGen / Euclid Engine.
//
// This preserves the production KeyboardInput class name so EuclidEngine and
// TheArbiter can continue using:
//
//      KeyboardInput m_keyboard;
//      KeyboardInput::KeyEvent
//
// without needing a larger Phase 1 rewrite.
// -----------------------------------------------------------------------------
class KeyboardInput {
public:
	enum KeySignal {
		KEY_NONE = 0,

		// Core Euclid / VitruGen controls
		KEY_ESCAPE,
		KEY_ENTER,
		KEY_SPACE,
		KEY_TAB,

		KEY_A,
		KEY_D,
		KEY_E,
		KEY_Q,
		KEY_W,
		KEY_S,

		KEY_0,
		KEY_1,
		KEY_2,
		KEY_3,
		KEY_4,
		KEY_5,
		KEY_6,
		KEY_7,
		KEY_8,

		// Volume / Marching Cubes standby controls
		KEY_V,      // volume render mode
		KEY_F,      // slice / field diagnostic mode
		KEY_R,      // raycast / render mode
		KEY_Z,      // reset camera / render parameters
		KEY_PLUS,   // optional zoom-in legacy support
		KEY_MINUS,  // optional zoom-out legacy support

		KEY_UNKNOWN
	};

	struct KeyEvent {
		KeySignal signal = KEY_NONE;
		unsigned char rawKey = 0;
		int x = 0;
		int y = 0;
	};

	KeyboardInput() {}
	~KeyboardInput() {}

	KeyEvent onKey(unsigned char key, int x, int y) const {
		KeyEvent event;
		event.rawKey = key;
		event.x = x;
		event.y = y;
		event.signal = decode(key);

		return event;
	}

private:
	KeySignal decode(unsigned char key) const {
		if (key == 27) {
			return KEY_ESCAPE;
		}

		if (key == 13) {
			return KEY_ENTER;
		}

		if (key == ' ') {
			return KEY_SPACE;
		}

		if (key == '\t') {
			return KEY_TAB;
		}

		if (key == '+' || key == '=') {
			return KEY_PLUS;
		}

		if (key == '-' || key == '_') {
			return KEY_MINUS;
		}
		if (key == '0') return KEY_0;
		if (key == '1') return KEY_1;
		if (key == '2') return KEY_2;
		if (key == '3') return KEY_3;
		if (key == '4') return KEY_4;
		if (key == '5') return KEY_5;
		if (key == '6') return KEY_6;
		if (key == '7') return KEY_7;
		if (key == '8') return KEY_8;

		const unsigned char lowered =
			static_cast<unsigned char>(
				std::tolower(static_cast<unsigned char>(key))
				);

		switch (lowered) {
		case 'a':
			return KEY_A;

		case 'd':
			return KEY_D;

		case 'e':
			return KEY_E;

		case 'q':
			return KEY_Q;

		case 'w':
			return KEY_W;

		case 's':
			return KEY_S;

		case 'v':
			return KEY_V;

		case 'f':
			return KEY_F;

		case 'r':
			return KEY_R;

		case 'z':
			return KEY_Z;

		default:
			return KEY_UNKNOWN;
		}
	}
};

// -----------------------------------------------------------------------------
// MouseInput
// -----------------------------------------------------------------------------
// Unified mouse input handler for VitruGen / Euclid Engine.
//
// This preserves the production MouseInput class name so EuclidEngine can keep:
//
//      MouseInput m_mouse;
//
// This version also preserves right-click for the GLUT menu. That matters
// because production currently attaches the GLUT menu to GLUT_RIGHT_BUTTON.
// -----------------------------------------------------------------------------
class MouseInput {
public:
	enum MouseMode {
		M_NONE = 0,
		M_VIEW = 1,
		M_MOVE = 2
	};

	MouseInput() {}
	~MouseInput() {}

	bool onButton(
		int button,
		int state,
		int x,
		int y,
		float cameraTrans[3]) {
		// Keep right-click available for the GLUT menu.
		if (button == GLUT_RIGHT_BUTTON) {
			return false;
		}

		// GLUT commonly reports wheel up/down as button 3 and 4.
		if (button == 3 || button == 4) {
			if (button == 3) {
				cameraTrans[2] += 0.1f * std::fabs(cameraTrans[2]);
			}
			else {
				cameraTrans[2] -= 0.1f * std::fabs(cameraTrans[2]);
			}

			return true;
		}

		m_buttonState = button;

		if (state == GLUT_DOWN) {
			if (button == GLUT_LEFT_BUTTON) {
				m_mode = M_VIEW;
			}
			else {
				m_mode = M_NONE;
				return false;
			}

			m_ox = x;
			m_oy = y;

			return true;
		}

		if (state == GLUT_UP) {
			m_mode = M_NONE;
			m_buttonState = -1;

			m_ox = x;
			m_oy = y;

			return true;
		}

		m_ox = x;
		m_oy = y;

		return false;
	}

	bool onMotion(
		int x,
		int y,
		float cameraRot[3],
		float orbitSensitivity = 1.0f) {
		const float dx = static_cast<float>(x - m_ox);
		const float dy = static_cast<float>(y - m_oy);

		if (orbitSensitivity < 0.0f) {
			orbitSensitivity = 0.0f;
		}

		if (m_mode == M_VIEW && m_buttonState == GLUT_LEFT_BUTTON) {
			cameraRot[0] += (dy / 5.0f) * orbitSensitivity;
			cameraRot[1] += (dx / 5.0f) * orbitSensitivity;
		}
		else {
			m_ox = x;
			m_oy = y;
			return false;
		}

		m_ox = x;
		m_oy = y;

		return true;
	}

	bool onPassiveMotion(int x, int y) {
		m_ox = x;
		m_oy = y;

		return true;
	}

	int getLastX() const { return m_ox; }
	int getLastY() const { return m_oy; }
	int getButtonState() const { return m_buttonState; }
	MouseMode getMode() const { return m_mode; }

private:
	int m_ox = 0;
	int m_oy = 0;
	int m_buttonState = -1;
	MouseMode m_mode = M_NONE;
};

#endif