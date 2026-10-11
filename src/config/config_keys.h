#ifndef CONFIG_CONFIG_KEYS_H
#define CONFIG_CONFIG_KEYS_H

#include <string>

#include "config/config.h"

namespace config
{
	// window
	/*
		The window geometry, remembered from the last session. The width and
		the height are the size of everything *below the title bar*: with the
		system title bar that is the whole client area, and with the drawn one
		it is the client area minus the menu bar that replaces it. The number
		therefore means the same thing whichever bar is in use, and turning the
		drawn one on does not shrink what is under it.

		The size is the *resting* one - the window as it is when it is neither
		maximized nor minimized - and the maximized flag is kept beside it, so
		a session that ended maximized opens maximized at the size it will
		return to.
	*/
	inline const key<int> WindowWidth{"uniteed_window_width", 1360};
	inline const key<int> WindowHeight{"uniteed_window_height", 860};
	inline const key<bool> WindowMaximized{"uniteed_window_maximized", false};

	// editor
	inline const key<std::string> DefaultTarget{"uniteed_default_target", "08"};
	inline const key<bool> ShowGrid{"uniteed_show_grid", true};
	inline const key<bool> ShowDetail{"uniteed_show_detail", true};
	inline const key<int> BrushSize{"uniteed_brush_size", 1};
	inline const key<std::string> LastMap{"uniteed_last_map", ""};
	inline const key<std::string> AssetVersion{"uniteed_asset_version", ""};

	/*
		How many pixels per layout pixel the vector drawings of the interface
		are rasterized at, on top of the density of the display. 1.0 matches
		the display exactly, which is what a scaled display wants; 2.0 gives
		an icon four times the pixels, which is smoother and costs four times
		the texture memory. The layout itself is not scaled by this.
	*/
	inline const key<float> UiScale{"uniteed_ui_scale", 1.0f};

	/*
		Whether the window is drawn without its system title bar. The editor
		then draws the title bar itself, in the same row as the toolbar, which
		is what puts the file operations and the panel shortcuts together at
		the top of the window; the drag, the resize edges and the window
		buttons come from the hit test the engine installs. A platform that
		cannot hit test a window keeps its decorations whatever this says,
		because a window nobody can move is worse than a title bar.
	*/
	inline const key<bool> ClientDecorations{"uniteed_client_decorations", true};

	// fonts: override the per platform lookup; empty means "ask the system" and
	// a value is a full path to a .ttf, .ttc or .otf
	inline const key<std::string> FontPath{"uniteed_font", ""};
	inline const key<std::string> FontPathBold{"uniteed_font_bold", ""};
	// the face the interface falls back to for CJK labels
	inline const key<std::string> FontPathCjk{"uniteed_font_cjk", ""};

	// logging: trace, info, warn or error
	inline const key<std::string> LogLevel{"uniteed_log_level", "info"};
} // namespace config

#endif // CONFIG_CONFIG_KEYS_H
