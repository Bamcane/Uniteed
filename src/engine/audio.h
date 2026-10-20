#ifndef ENGINE_AUDIO_H
#define ENGINE_AUDIO_H

/*
	Sound preview: the editor can play a map's .opus resources while a
	sound layer is edited, which is what the DDRaceNetwork editor does and
	what makes placing a sound source possible without a client to test in.

	Why opusfile and not libopus directly: a map's sound resources are Ogg
	Opus files, and opusfile is the decoder that reads the container as well
	as the packets. Why SDL3 and not a second audio library: the editor
	window is already an SDL3 program, so the mixer that comes with it costs
	one subsystem init and nothing else.
*/

#include <mutex>
#include <string>
#include <string_view>

#include <sugar_types.h>

// storage::byte_vector is part of the interface, so the header needs it even
// though it is only a typedef here
#include "base/storage.h"

namespace engine
{
	class CAudio
	{
	public:
		CAudio();
		~CAudio();
		CAudio(const CAudio &) = delete;
		CAudio &operator=(const CAudio &) = delete;

		// opens the device; returns false and is silent when there is none
		bool Init();
		void Shutdown();
		bool Available() const;

		// plays the bytes of an .opus file, replacing whatever is playing
		bool Play(std::string_view Name, const storage::byte_vector &Opus);
		bool PlayFile(std::string_view Name, const std::string &Path);
		void Stop();
		bool IsPlaying() const;
		std::string_view NowPlaying() const;
		// how long the current sound is, seconds (0 when unknown)
		float Duration() const;
		float Position() const;

		// Decodes for the SDL callback and pushes into its stream. It is public
		// for the same reason the thunk beside it is not a member: the SDL
		// callback type needs SDL types, and this header stays free of SDL.
		static void DecodeCallback(void *pUserdata, void *pStream, float *pPcm, int Additional);

	private:
		// the decoded file, as an opaque pointer so the header stays free of
		// opusfile and SDL
		void *m_pStream = nullptr;
		void *m_pFile = nullptr;
		std::string m_Name;
		// seconds, from op_pcm_total()
		float m_Duration = 0.0f;
		bool m_Available = false;

		/*
			The decoder lives behind UNITEED_GUI, not behind UNITEED_AUDIO: the
			second is a define CMake sets for the executable only, so a unit
			that includes this header without it would lay out CAudio - and with
			it every CEditor member after its m_Audio - differently from the
			objects it gets linked against. A headless build that does not link
			a decoder never calls these members, so leaving them out by the
			window switch is both safe and enough.
		*/
#if UNITEED_GUI
		// SDL decodes on its own thread, so the callback and the public methods
		// share everything below through this lock
		mutable std::mutex m_Lock;
		// the byte_vector op_open_memory borrows: it does NOT copy the buffer,
		// so it has to outlive m_pFile
		storage::byte_vector m_Data;
		// frames (one sample per channel) handed to SDL, for Position()
		long long m_FramesDecoded = 0;
		// frames the file holds in total
		long long m_TotalFrames = 0;
		// set by Stop()/Shutdown(), the callback returns immediately
		bool m_Stop = false;
		// set when the decoder reached the end of the file or failed
		bool m_Finished = false;
		// a mono file is upmixed by op_read_float_stereo(), only for the log
		bool m_Mono = false;
#endif
	};

	/*
		The length of an opus file, read from the decoder rather than guessed.
		It lives here rather than in the editor because it is the one thing the
		headless build cannot do, and the editor asks for it through a compile
		time check instead of linking a decoder of its own.
	*/
	float OpusDurationSeconds(const storage::byte_vector &Opus);
} // namespace engine

#endif // ENGINE_AUDIO_H
