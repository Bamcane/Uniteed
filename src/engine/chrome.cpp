#include <algorithm>

#include "engine/chrome.h"

namespace engine
{
	window_region HitTestChrome(const CClientChrome &Chrome, const base::vec2 &WindowSize, const base::vec2 &Point)
	{
		if(!Chrome.m_bEnabled || WindowSize.x <= 1.0f || WindowSize.y <= 1.0f)
			return window_region::normal;

		// a control wins over the frame: the close button sits in the corner,
		// and it has to stay a button there
		for(const base::rect &Control : Chrome.m_vControls)
			if(Control.Contains(Point))
				return window_region::normal;

		/*
			The frame around the window, which is what the system title bar
			would have brought with it. It is deliberately narrow: the tool
			strip owns the left edge of the window and its first button starts
			right after it.
		*/
		const float Edge = std::max(Chrome.m_Edge, 1.0f);
		const bool bLeft = Point.x <= Edge;
		const bool bRight = Point.x >= WindowSize.x - Edge;
		const bool bTop = Point.y <= Edge;
		const bool bBottom = Point.y >= WindowSize.y - Edge;
		if(bLeft || bRight || bTop || bBottom)
		{
			if(bTop && bLeft)
				return window_region::resize_top_left;
			if(bTop && bRight)
				return window_region::resize_top_right;
			if(bBottom && bLeft)
				return window_region::resize_bottom_left;
			if(bBottom && bRight)
				return window_region::resize_bottom_right;
			if(bTop)
				return window_region::resize_top;
			if(bBottom)
				return window_region::resize_bottom;
			return bLeft ? window_region::resize_left : window_region::resize_right;
		}

		// the bar itself drags the window, which is what makes a drawn title
		// bar behave like a real one
		if(Chrome.m_HeaderHeight > 0.0f && Point.y < Chrome.m_HeaderHeight)
			return window_region::drag;
		return window_region::normal;
	}
} // namespace engine
