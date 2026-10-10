#ifndef GAME_MAP_ENVELOPE_H
#define GAME_MAP_ENVELOPE_H

/*
	Envelope evaluation, a reimplementation of what the reference renderer does
	(upstream Teeworlds and DDRaceNetwork share one implementation between the
	client and their editor).

	The rules that matter and are easy to get wrong:

	  * the time wraps at the last point, so an envelope loops by itself
	  * before the first point, and for a single point envelope, the value of the
	    last (respectively the only) point is used
	  * the curve type of the point *before* the time decides the interpolation
	  * values are 22.10 fixed point like every other coordinate in the format,
	    so 1024 is 1.0, and an envelope colour of 1024,0,0 is red
	  * a quad position envelope is in world units, one tile being 32 of them
*/

#include <cstddef>

#include "game/map/map.h"

namespace game
{
	struct CEnvelopeSample
	{
		float m_aChannels[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		float &operator[](std::size_t Channel) { return m_aChannels[Channel]; }
		float operator[](std::size_t Channel) const { return m_aChannels[Channel]; }
	};

	// Evaluates Channels channels of the envelope at Time in seconds. An empty
	// envelope, or an index the map does not have, leaves the sample untouched,
	// which is how a caller gets the default colour (1,1,1,1) for a quad.
	void EvaluateEnvelope(const CEnvelope &Envelope, float Time, CEnvelopeSample &Sample, int Channels);

	// The last point's time in seconds: the natural loop length.
	float EnvelopeLength(const CEnvelope &Envelope);
} // namespace game

#endif // GAME_MAP_ENVELOPE_H
