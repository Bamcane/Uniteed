#ifndef ENGINE_EDITOR_APP_H
#define ENGINE_EDITOR_APP_H

/*
	The editor application: window, main loop and the offscreen screenshot mode
	used to smoke test the interface without a display interaction.
*/

#include <string>
#include <vector>

#include "base/math.h"

namespace game
{
	class CEditor;
}

namespace engine
{
	/*
		The state the screenshot mode sets up before it renders. The editor
		window is immediate mode, so outside a test there is no scripting
		layer: these options are the only way to pin the interface into a known
		state and compare one run with the next.
	*/
	struct CScreenshotSetup
	{
		// -1 keeps whatever the editor picked by itself
		int m_Group = -1;
		int m_Layer = -1;
		// which quad of a quad layer to select; -1 asks for the first one, and
		// -2 for no selection at all
		int m_Quad = -1;
		// the corner the colour picker is aimed at, -1 for all four
		int m_Corner = -1;
		bool m_bOpenColorPicker = false;
		// the envelope preview position, seconds; negative keeps 0
		float m_EnvelopeTime = -1.0f;
		/*
			Which workspace each dock shows, by name: "layers", "tiles",
			"envelopes" or "rules". Empty leaves whatever the editor picked,
			which is why the screenshot mode can capture either layout without
			the caller knowing the enum.
		*/
		std::string m_LeftDock;
		std::string m_RightDock;
		// which envelope the curve workspace edits; negative keeps the selection
		int m_Envelope = -1;
		int m_EnvelopePoint = -1;
		/*
			The scale the vector drawings are rasterized at, which is the knob
			the icons follow: 1 is the ordinary case, and 2 asks for the same
			icons at twice the resolution, which is what a test uses to see the
			textures be re-made without a second monitor.
		*/
		float m_UiScale = 0.0f;
		/*
			A density to switch to *after* the first frame, which is what makes
			the icons rasterize themselves again: it is the path a window dragged
			to a denser monitor takes, and the only way to see it on a machine
			with one display. 0 leaves the display alone.
		*/
		float m_UiScaleChange = 0.0f;
		/*
			A file to open *after* the first frame, which is how the offscreen
			mode reproduces the thing a user does with File > Open: the first
			map is drawn, then another one replaces it. Everything the editor
			caches by texture handle has to survive that, and this is what makes
			the difference visible in a picture instead of in a bug report.
		*/
		std::string m_OpenAfter;
		/*
			Whether the window is drawn without its system title bar: -1 keeps
			the config, 0 asks for the system one, 1 for the drawn one. It is
			here rather than in the config alone because a compositor that will
			not let a borderless window be moved is a thing a user has to be
			able to walk away from with one flag.
		*/
		int m_ClientDecorations = -1;
		/*
			A menu of the title bar row to open before the frame is drawn:
			"files" or "settings". It is the offscreen way of taking a picture
			of one, without a script that has to know where the button is.
		*/
		std::string m_Menu;
		/*
			How many frames a windowed session runs before it quits as if the
			window had been closed. 0 runs until the user closes it; a test uses
			a small number to reach the end of a session, which is where the
			settings are written.
		*/
		int m_QuitAfter = 0;
		/*
			The language of the interface, as a catalog id ("en", "zh-Hans").
			Empty takes the settings file, and the desktop's own locale when
			that says nothing: it is here as well as in the settings because a
			picture of the interface in another language has to be reproducible
			from a command line.
		*/
		std::string m_Language;
	};

	// an empty map path starts with a new map; a non empty screenshot path
	// renders one frame into that PNG and exits
	int RunEditor(const std::string &MapPath, const std::string &ScreenshotPath = "",
		const CScreenshotSetup &Setup = CScreenshotSetup{});

	// puts the editor into the state the setup describes, which is also what a
	// caller that wants to draw over the frame uses
	void ApplyScreenshotSetup(game::CEditor &Editor, const CScreenshotSetup &Setup);

	/*
		A script of input events for the offscreen mode, so the interface can be
		pushed through a sequence of clicks and keys without a mouse. Each entry
		is one frame: the editor reads the state once per frame, exactly as it
		does in the window.
	*/
	struct CInputStep
	{
		base::vec2 m_Move{-1.0f, -1.0f};  // -1 keeps the previous position
		// a point of the selected quad to move onto instead of a pixel: the
		// canvas hit tests in screen space, so a script that means "the top
		// right corner" asks the editor where that is rather than carrying a
		// coordinate a layout change would invalidate. x < 0 for none.
		base::ivec2 m_QuadPoint{-1, -1};
		int m_PressButton = 0;            // SDL button number, 0 for none
		int m_ReleaseButton = 0;
		// the platform's click count: 2 is the second press of a double click,
		// which is what a name field in the tree opens on
		int m_Clicks = 1;
		// text as the keyboard would type it, for the fields that read it
		std::string m_TypeText;
		int m_PressKey = 0;               // SDLK_* value, 0 for none
		int m_ReleaseKey = 0;
		int m_Modifiers = 0;              // SDL_KMOD_*
		int m_Scroll = 0;
	};

	// the scripted run: the editor is started in the setup state, every step is
	// played as one frame, and the last frame is written to ScreenshotPath
	int RunEditorScript(const std::string &MapPath, const std::string &ScreenshotPath,
		const CScreenshotSetup &Setup, const std::vector<CInputStep> &Steps);
} // namespace engine

#endif // ENGINE_EDITOR_APP_H
