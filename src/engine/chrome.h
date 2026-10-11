#ifndef ENGINE_CHROME_H
#define ENGINE_CHROME_H

/*
	The shape of a window that draws its own decorations.

	It lives on its own, apart from the graphics that use it, for two reasons.
	The first is that it has no dependency on a window at all: it is a plain
	description of where the title bar is and which parts of it are buttons,
	plus the function that says what a point of that window is - the bar, the
	frame, or nothing - so a test can check the whole policy with no display.

	The second is that a window without a system title bar has to give the
	window manager back what the title bar used to provide: dragging it,
	resizing it from its edges, and keeping the buttons in that bar clickable.
	That translation belongs to the platform layer (engine/graphics.cpp); this
	is the part that decides.
*/

#include <vector>

#include "base/math.h"

namespace engine
{
	// what the pointer is over in a client decorated window
	enum class window_region
	{
		normal,
		drag,
		resize_top,
		resize_bottom,
		resize_left,
		resize_right,
		resize_top_left,
		resize_top_right,
		resize_bottom_left,
		resize_bottom_right,
	};

	struct CClientChrome
	{
		// false leaves the window to the system decorations, and the hit test
		// is not installed at all
		bool m_bEnabled = false;
		// the drawn title bar: above this line is window furniture, below it
		// the application's own panels
		float m_HeaderHeight = 0.0f;
		// how wide the grabbable frame around the window is
		float m_Edge = 4.0f;
		/*
			The buttons in the header. A control wins over the frame, because
			the close button lives in the corner: were the frame tested first,
			it could never be clicked.
		*/
		std::vector<base::rect> m_vControls;
	};

	// which piece of window furniture a point belongs to
	window_region HitTestChrome(const CClientChrome &Chrome, const base::vec2 &WindowSize, const base::vec2 &Point);
} // namespace engine

#endif // ENGINE_CHROME_H
