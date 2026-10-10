#ifndef BASE_MATH_H
#define BASE_MATH_H

/*
	A deliberately tiny math layer instead of a full linear algebra library:
	the editor only needs 2D vectors, rectangles and a screen/world transform.
*/

#include <algorithm>
#include <cmath>

namespace base
{
	inline constexpr float Pi = 3.14159265358979323846f;

	template<typename T>
	struct vector2
	{
		T x = T(0);
		T y = T(0);

		constexpr vector2() = default;
		constexpr vector2(T X, T Y) :
			x(X), y(Y) {}

		constexpr vector2 operator+(const vector2 &o) const { return {x + o.x, y + o.y}; }
		constexpr vector2 operator-(const vector2 &o) const { return {x - o.x, y - o.y}; }
		constexpr vector2 operator*(T s) const { return {x * s, y * s}; }
		constexpr vector2 operator/(T s) const { return {x / s, y / s}; }
		constexpr vector2 &operator+=(const vector2 &o) { x += o.x; y += o.y; return *this; }
		constexpr vector2 &operator-=(const vector2 &o) { x -= o.x; y -= o.y; return *this; }
		constexpr bool operator==(const vector2 &o) const { return x == o.x && y == o.y; }
	};

	using vec2 = vector2<float>;
	using ivec2 = vector2<int>;

	template<typename T>
	inline T clamp(T Value, T Low, T High) { return Value < Low ? Low : (Value > High ? High : Value); }

	inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

	// 22.10 fixed point, the unit Teeworlds stores coordinates in
	inline constexpr float FixedPointScale = 1.0f / 1024.0f;
	inline float FixedToFloat(int Value) { return Value * FixedPointScale; }
	inline int FloatToFixed(float Value) { return static_cast<int>(std::round(Value * 1024.0f)); }

	struct rect
	{
		vec2 m_TopLeft;
		vec2 m_BottomRight;

		constexpr rect() = default;
		constexpr rect(vec2 TopLeft, vec2 BottomRight) :
			m_TopLeft(TopLeft), m_BottomRight(BottomRight) {}

		float Width() const { return m_BottomRight.x - m_TopLeft.x; }
		float Height() const { return m_BottomRight.y - m_TopLeft.y; }
		vec2 Center() const { return (m_TopLeft + m_BottomRight) * 0.5f; }

		bool Contains(vec2 p) const
		{
			return p.x >= m_TopLeft.x && p.x <= m_BottomRight.x && p.y >= m_TopLeft.y && p.y <= m_BottomRight.y;
		}

		rect Intersect(const rect &o) const
		{
			return {vec2(std::max(m_TopLeft.x, o.m_TopLeft.x), std::max(m_TopLeft.y, o.m_TopLeft.y)),
				vec2(std::min(m_BottomRight.x, o.m_BottomRight.x), std::min(m_BottomRight.y, o.m_BottomRight.y))};
		}

		static rect FromSize(vec2 Pos, vec2 Size) { return {Pos, Pos + Size}; }
	};

	// camera / viewport transform: world pixels <-> screen pixels
	struct view_transform
	{
		vec2 m_Center;   // world position at the center of the viewport
		vec2 m_Origin;   // top left of the viewport in window pixels
		vec2 m_Viewport; // size of the viewport in pixels
		float m_Zoom = 1.0f;

		// The viewport is a sub rectangle of the window (the canvas sits between
		// the panels), so the origin has to be part of both directions. Drawing
		// and hit testing go through these two, which is what keeps the tile
		// under the cursor the tile that gets painted.
		vec2 WorldToScreen(vec2 World) const
		{
			return {m_Origin.x + (World.x - m_Center.x) * m_Zoom + m_Viewport.x * 0.5f,
				m_Origin.y + (World.y - m_Center.y) * m_Zoom + m_Viewport.y * 0.5f};
		}

		vec2 ScreenToWorld(vec2 Screen) const
		{
			return {(Screen.x - m_Origin.x - m_Viewport.x * 0.5f) / m_Zoom + m_Center.x,
				(Screen.y - m_Origin.y - m_Viewport.y * 0.5f) / m_Zoom + m_Center.y};
		}

		// the visible world rectangle
		rect VisibleWorld() const
		{
			vec2 Half = m_Viewport * (0.5f / m_Zoom);
			return {m_Center - Half, m_Center + Half};
		}
	};
} // namespace base

#endif // BASE_MATH_H
