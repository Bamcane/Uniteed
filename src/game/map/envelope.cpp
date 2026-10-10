#include "game/map/envelope.h"

#include <algorithm>
#include <cmath>

#include "base/math.h"

namespace game
{
	namespace
	{
		// the cubic bezier of the reference, y(t)
		float Bezier(float p0, float p1, float p2, float p3, float t)
		{
			const float Inv = 1.0f - t;
			return p0 * Inv * Inv * Inv + 3.0f * p1 * t * Inv * Inv + 3.0f * p2 * t * t * Inv + p3 * t * t * t;
		}

		/*
			Solves x(a) = x for the monotonic cubic bezier through
			(p0, p1, p2, p3), the same Cardano based solution the reference uses
			so that a bezier envelope bends identically in both.
		*/
		float SolveBezier(float x, float p0, float p1, float p2, float p3)
		{
			const double x3 = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
			const double x2 = 3.0 * p0 - 6.0 * p1 + 3.0 * p2;
			const double x1 = -3.0 * p0 + 3.0 * p1;
			const double x0 = p0 - x;

			if(x3 == 0.0 && x2 == 0.0)
			{
				const double a = x1;
				const double b = x0;
				if(a == 0.0)
					return 0.0f;
				return static_cast<float>(-b / a);
			}
			if(x3 == 0.0)
			{
				const double b = x1 / x2;
				const double c = x0 / x2;
				if(c == 0.0)
					return 0.0f;
				const double D = b * b - 4.0 * c;
				const double SqrtD = std::sqrt(D);
				const double t = (-b + SqrtD) / 2.0;
				if(0.0 <= t && t <= 1.0001)
					return static_cast<float>(t);
				return static_cast<float>((-b - SqrtD) / 2.0);
			}

			const double a = x2 / x3;
			const double b = x1 / x3;
			const double c = x0 / x3;

			const double Substitute = a / 3.0;
			const double p = b / 3.0 - a * a / 9.0;
			const double q = (2.0 * a * a * a / 27.0 - a * b / 3.0 + c) / 2.0;
			const double D = q * q + p * p * p;

			if(D > 0.0)
			{
				const double s = std::sqrt(D);
				return static_cast<float>(std::cbrt(s - q) - std::cbrt(s + q) - Substitute);
			}
			if(D == 0.0)
			{
				const double s = std::cbrt(-q);
				const double t = 2.0 * s - Substitute;
				if(0.0 <= t && t <= 1.0001)
					return static_cast<float>(t);
				return static_cast<float>(-s - Substitute);
			}

			const double Phi = std::acos(-q / std::sqrt(-(p * p * p))) / 3.0;
			const double s = 2.0 * std::sqrt(-p);

			const double t1 = s * std::cos(Phi) - Substitute;
			if(0.0 <= t1 && t1 <= 1.0001)
				return static_cast<float>(t1);

			const double t2 = -s * std::cos(Phi + base::Pi / 3.0) - Substitute;
			if(0.0 <= t2 && t2 <= 1.0001)
				return static_cast<float>(t2);

			return static_cast<float>(-s * std::cos(Phi - base::Pi / 3.0) - Substitute);
		}

		/*
			The index of the point the time falls into: the last point whose time
			is not after it, clamped so that there is always a point after it.
			-1 means the time is before the first point.
		*/
		int FindPoint(const std::vector<CEnvelopePoint> &Points, float Time)
		{
			if(Points.empty() || Time < static_cast<float>(Points.front().m_Time))
				return -1;
			std::size_t Index = 0;
			for(std::size_t i = 1; i < Points.size(); i++)
			{
				if(static_cast<float>(Points[i].m_Time) > Time)
					break;
				Index = i;
			}
			return static_cast<int>(std::min(Index, Points.size() - 2));
		}
	} // namespace

	void EvaluateEnvelope(const CEnvelope &Envelope, float Time, CEnvelopeSample &Sample, int Channels)
	{
		const std::vector<CEnvelopePoint> &Points = Envelope.m_vPoints;
		Channels = std::min(Channels, std::min(Envelope.m_Channels, 4));
		if(Points.empty() || Channels <= 0)
			return;

		// the caller works in seconds, the format stores milliseconds
		float TimeMillis = Time * 1000.0f;

		if(Points.size() == 1)
		{
			for(int c = 0; c < Channels; c++)
				Sample[c] = base::FixedToFloat(Points[0].m_aValues[c]);
			return;
		}

		const CEnvelopePoint &Last = Points.back();
		if(Last.m_Time > 0)
			TimeMillis = std::fmod(TimeMillis, static_cast<float>(Last.m_Time));
		else
			TimeMillis = 0.0f;
		if(TimeMillis < 0.0f)
			TimeMillis = 0.0f;

		const int Found = FindPoint(Points, TimeMillis);
		if(Found == -1)
		{
			for(int c = 0; c < Channels; c++)
				Sample[c] = base::FixedToFloat(Last.m_aValues[c]);
			return;
		}

		const CEnvelopePoint &Current = Points[static_cast<std::size_t>(Found)];
		const CEnvelopePoint &Next = Points[static_cast<std::size_t>(Found) + 1];
		const int Delta = Next.m_Time - Current.m_Time;
		if(Delta <= 0)
		{
			for(int c = 0; c < Channels; c++)
				Sample[c] = base::FixedToFloat(Current.m_aValues[c]);
			return;
		}

		float a = (TimeMillis - static_cast<float>(Current.m_Time)) / static_cast<float>(Delta);

		switch(Current.m_Curvetype)
		{
		case CURVETYPE_STEP:
			a = 0.0f;
			break;
		case CURVETYPE_SLOW:
			a = a * a * a;
			break;
		case CURVETYPE_FAST:
			a = 1.0f - a;
			a = 1.0f - a * a * a;
			break;
		case CURVETYPE_SMOOTH:
			a = -2.0f * a * a * a + 3.0f * a * a;
			break;
		case CURVETYPE_BEZIER:
			for(int c = 0; c < Channels; c++)
			{
				const float p0x = static_cast<float>(Current.m_Time);
				const float p3x = static_cast<float>(Next.m_Time);
				// the tangents are relative to their own point
				const float p0y = base::FixedToFloat(Current.m_aValues[c]);
				const float p3y = base::FixedToFloat(Next.m_aValues[c]);
				float p1x = p0x + base::FixedToFloat(Current.m_Bezier.m_aOutTangentdx[c]);
				float p2x = p3x + base::FixedToFloat(Next.m_Bezier.m_aInTangentdx[c]);
				const float p1y = p0y + base::FixedToFloat(Current.m_Bezier.m_aOutTangentdy[c]);
				const float p2y = p3y + base::FixedToFloat(Next.m_Bezier.m_aInTangentdy[c]);

				// a monotonic curve may not overshoot in time
				p1x = base::clamp(p1x, p0x, p3x);
				p2x = base::clamp(p2x, p0x, p3x);

				const float t = base::clamp(SolveBezier(TimeMillis, p0x, p1x, p2x, p3x), 0.0f, 1.0f);
				Sample[c] = Bezier(p0y, p1y, p2y, p3y, t);
			}
			return;
		case CURVETYPE_LINEAR:
		default:
			break;
		}

		for(int c = 0; c < Channels; c++)
		{
			const float v0 = base::FixedToFloat(Current.m_aValues[c]);
			const float v1 = base::FixedToFloat(Next.m_aValues[c]);
			Sample[c] = v0 + (v1 - v0) * a;
		}
	}

	float EnvelopeLength(const CEnvelope &Envelope)
	{
		if(Envelope.m_vPoints.empty())
			return 0.0f;
		return static_cast<float>(Envelope.m_vPoints.back().m_Time) / 1000.0f;
	}
} // namespace game
