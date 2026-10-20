#include "audio.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

#include "base/logger.h"
#include "base/storage.h"

#if defined(UNITEED_AUDIO)

#include <SDL3/SDL.h>
#include <opus/opusfile.h>

namespace engine
{
	namespace
	{
		// one decode chunk, in frames: 4096 frames is ~85 ms at the 48 kHz Opus
		// always decodes to, small enough to react to a stop promptly and
		// large enough not to call the decoder too often
		constexpr int DECODE_FRAMES = 4096;
		// stereo samples, the unit op_read_float_stereo() and
		// SDL_PutAudioStreamData() both work in
		constexpr int DECODE_SAMPLES = DECODE_FRAMES * 2;
		// the stream is F32 stereo, so a frame is two floats
		constexpr int BYTES_PER_FRAME = static_cast<int>(sizeof(float)) * 2;
		constexpr float SAMPLE_RATE = 48000.0f;

		constexpr const char *TAG = "audio";
	} // namespace

	/*
		The callback SDL calls on its audio thread, in SDL's own signature. It
		forwards to the decoding half, which is a member so it may touch the
		private state. It only ever pushes data: nothing here tears the stream
		down, and SDL serialises the callback against the main thread's calls
		on the same stream, so a Put never overlaps a Clear from Play/Stop.
	*/
	void SDLCALL AudioDecodeThunk(void *pUserdata, SDL_AudioStream *pStream, int Additional, int Total)
	{
		(void)Total;
		// DecodeCallback drops anything past Additional, so this buffer only
		// has to be big enough for the largest request SDL makes
		static thread_local float pPcm[2 * 16384];
		CAudio::DecodeCallback(pUserdata, pStream, pPcm, Additional);
	}

	/*
		Reads just the length, for the editor's sound length column: opening
		decodes the header, op_pcm_total() then knows the frame count, and the
		file object is freed again without playing anything.
	*/
	float OpusDurationSeconds(const storage::byte_vector &Opus)
	{
		if(Opus.empty())
			return 0.0f;
		int Error = 0;
		OggOpusFile *pFile = op_open_memory(reinterpret_cast<const unsigned char *>(Opus.data()), Opus.size(), &Error);
		if(pFile == nullptr)
			return 0.0f;
		const ogg_int64_t Total = op_pcm_total(pFile, -1);
		op_free(pFile);
		if(Total <= 0)
			return 0.0f;
		return static_cast<float>(Total) / SAMPLE_RATE;
	}

	void CAudio::DecodeCallback(void *pUserdata, void *pStream, float *pPcm, int Additional)
	{
		CAudio *pAudio = static_cast<CAudio *>(pUserdata);
		int Available = Additional;
		if(Available <= 0 || pPcm == nullptr)
			return;

		std::lock_guard<std::mutex> Guard(pAudio->m_Lock);
		if(pAudio->m_pFile == nullptr || pAudio->m_Stop)
			return;

		OggOpusFile *pFile = static_cast<OggOpusFile *>(pAudio->m_pFile);
		while(Available > 0)
		{
			const int Samples = op_read_float_stereo(pFile, pPcm, DECODE_SAMPLES);
			if(Samples < 0)
			{
				// the file is corrupt past this point, the rest would only be
				// noise, so this is the end of the sound
				LOG_WARN(TAG, "decoder error {:d}, stopping '{}'", Samples, pAudio->m_Name.c_str());
				pAudio->m_Finished = true;
				break;
			}
			if(Samples == 0)
			{
				// end of file: keep what is already queued, i.e. do not clear
				// the stream from here, and let SDL mix it out
				pAudio->m_Finished = true;
				break;
			}

			int Bytes = Samples * static_cast<int>(sizeof(float));
			// only ever hand SDL as much as it just asked for; a bigger Put
			// would sit in the stream's buffer and could block this thread
			Bytes = std::min(Bytes, Available);
			// an odd byte cannot happen: both Samples and the cap are floats
			if(!SDL_PutAudioStreamData(static_cast<SDL_AudioStream *>(pStream), pPcm, Bytes))
			{
				LOG_WARN(TAG, "putting sound data failed: {}", SDL_GetError());
				pAudio->m_Finished = true;
				break;
			}
			pAudio->m_FramesDecoded += Bytes / BYTES_PER_FRAME;
			Available -= Bytes;
		}
	}

	CAudio::CAudio() = default;
	CAudio::~CAudio() { Shutdown(); }

	bool CAudio::Available() const
	{
		std::lock_guard<std::mutex> Guard(m_Lock);
		return m_Available;
	}

	bool CAudio::Init()
	{
		if(Available())
			return true;

		if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
		{
			// the one place this is logged: Play() is called on every click of
			// a sound layer and must not spam the log on a machine without a
			// sound card
			LOG_WARN(TAG, "no audio device, sound preview disabled: {}", SDL_GetError());
			return false;
		}

		// the decoder always produces this format, so the stream needs no
		// conversion at all; SDL_AudioStream does the conversion when a device
		// wants something else
		const SDL_AudioSpec Spec{SDL_AUDIO_F32, 2, 48000};
		m_pStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &Spec, &AudioDecodeThunk, this);
		if(m_pStream == nullptr)
		{
			LOG_WARN(TAG, "no audio device, sound preview disabled: {}", SDL_GetError());
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			return false;
		}

		// OpenAudioDeviceStream leaves the device paused on purpose, so
		// playback has to be resumed once
		if(!SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream *>(m_pStream)))
		{
			LOG_WARN(TAG, "resuming the audio device failed: {}", SDL_GetError());
			SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(m_pStream));
			m_pStream = nullptr;
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			return false;
		}

		{
			std::lock_guard<std::mutex> Guard(m_Lock);
			m_Available = true;
		}
		LOG_INFO(TAG, "audio device open, 48000 Hz float stereo");
		return true;
	}

	void CAudio::Shutdown()
	{
		SDL_AudioStream *pStream = nullptr;
		OggOpusFile *pFile = nullptr;
		{
			std::lock_guard<std::mutex> Guard(m_Lock);
			// the callback tests these first and returns, so nothing touches
			// the file or the stream after this point
			m_Stop = true;
			m_Available = false;
			m_Finished = true;
			pStream = static_cast<SDL_AudioStream *>(m_pStream);
			pFile = static_cast<OggOpusFile *>(m_pFile);
			m_pStream = nullptr;
			m_pFile = nullptr;
			m_Data.clear();
			m_Name.clear();
			m_Duration = 0.0f;
			m_TotalFrames = 0;
			m_FramesDecoded = 0;
		}

		if(pStream != nullptr)
		{
			/*
				The stream is destroyed *before* the subsystem is quit, in this
				order. SDL 3.4.18 fails inside SDL_QuitSubSystem when a stream
				created by SDL_OpenAudioDeviceStream is still alive (reproduced
				with a twenty line SDL-only program and the dummy driver, so it
				is not this module's state): the device teardown follows a
				pointer the stream owns. Destroying the stream first detaches it
				from the device.

				Shutdown() must therefore run *before* SDL_Quit(), because
				SDL_Quit tears the subsystem down underneath the live stream -
				which is what made the editor abort at exit. The editor calls
				ShutdownSoundPreview() on its way out for exactly that reason,
				and the destructor is only a backstop for a caller that forgot.
			*/
			SDL_PauseAudioStreamDevice(pStream);
			SDL_DestroyAudioStream(pStream);
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
		}
		if(pFile != nullptr)
			op_free(pFile);
	}

	bool CAudio::Play(std::string_view Name, const storage::byte_vector &Opus)
	{
		// replacing whatever plays starts by silencing and dropping it
		Stop();

		if(!Available())
			return false;
		if(Opus.empty())
		{
			LOG_WARN(TAG, "sound '{}' is empty", std::string(Name).c_str());
			return false;
		}

		{
			std::lock_guard<std::mutex> Guard(m_Lock);
			// op_open_memory does not copy the bytes, so they must stay alive
			// for as long as the file object does. Play(m_Data) would be a
			// self assignment, which vector handles, but clearing first would
			// then leave opusfile with a dangling pointer, so compare first.
			if(&Opus != &m_Data)
			{
				m_Data.clear();
				m_Data = Opus;
			}
			// whatever a leftover callback queued is not part of this sound
			m_FramesDecoded = 0;
			m_Finished = false;
			m_Stop = false;
		}

		int Error = 0;
		OggOpusFile *pFile = op_open_memory(reinterpret_cast<const unsigned char *>(m_Data.data()), m_Data.size(), &Error);
		if(pFile == nullptr)
		{
			LOG_WARN(TAG, "opening '{}' as Opus failed: {:d}", std::string(Name).c_str(), Error);
			{
				std::lock_guard<std::mutex> Guard(m_Lock);
				m_Data.clear();
			}
			return false;
		}

		const int Channels = op_channel_count(pFile, -1);
		if(Channels != 1 && Channels != 2)
		{
			// a map resources is meant to be mono or stereo; more channels
			// would be read as stereo and come out wrong, so refuse it
			LOG_WARN(TAG, "'{}' has {:d} channels, only mono and stereo are played", std::string(Name).c_str(), Channels);
			op_free(pFile);
			{
				std::lock_guard<std::mutex> Guard(m_Lock);
				m_Data.clear();
			}
			return false;
		}

		const ogg_int64_t Total = op_pcm_total(pFile, -1);
		float Duration = 0.0f;
		bool Mono = false;
		{
			std::lock_guard<std::mutex> Guard(m_Lock);
			m_pFile = pFile;
			m_Name.assign(Name);
			m_TotalFrames = Total > 0 ? static_cast<long long>(Total) : 0;
			m_Duration = Total > 0 ? static_cast<float>(Total) / SAMPLE_RATE : 0.0f;
			m_Mono = Channels == 1;
			// the log line below runs outside the lock, so read its values here
			Duration = m_Duration;
			Mono = m_Mono;
		}

		SDL_AudioStream *pStream = static_cast<SDL_AudioStream *>(m_pStream);
		if(pStream == nullptr || !SDL_ClearAudioStream(pStream) || !SDL_ResumeAudioStreamDevice(pStream))
		{
			LOG_WARN(TAG, "starting playback of '{}' failed: {}", std::string(Name).c_str(), SDL_GetError());
			Stop();
			return false;
		}

		// the device was resumed while empty, so the callback fires and decodes
		// the first chunks: no data has to be pushed from here
		LOG_INFO(TAG, "playing '{}', {:.2f} s{}", std::string(Name).c_str(), Duration, Mono ? " (mono upmixed)" : "");
		return true;
	}

	bool CAudio::PlayFile(std::string_view Name, const std::string &Path)
	{
		std::optional<storage::byte_vector> Opus = storage::read(Path);
		if(!Opus.has_value())
		{
			// storage::read already told why it could not read the file
			LOG_WARN(TAG, "cannot read sound '{}' from '{}'", storage::path_text(Name), storage::path_text(Path));
			return false;
		}
		return Play(Name, *Opus);
	}

	void CAudio::Stop()
	{
		SDL_AudioStream *pStream = static_cast<SDL_AudioStream *>(m_pStream);
		OggOpusFile *pFile = nullptr;
		{
			std::lock_guard<std::mutex> Guard(m_Lock);
			if(m_pFile == nullptr && !m_Finished)
				return;
			m_Stop = true;
			m_Finished = true;
			pFile = static_cast<OggOpusFile *>(m_pFile);
			m_pFile = nullptr;
			m_Data.clear();
			m_Name.clear();
			m_Duration = 0.0f;
			m_TotalFrames = 0;
			m_FramesDecoded = 0;
		}

		if(pStream != nullptr)
		{
			// drop what is still queued, so a new sound starts clean
			SDL_ClearAudioStream(pStream);
		}
		if(pFile != nullptr)
			op_free(pFile);
	}

	bool CAudio::IsPlaying() const
	{
		std::lock_guard<std::mutex> Guard(m_Lock);
		return m_pFile != nullptr && !m_Finished && !m_Stop;
	}

	std::string_view CAudio::NowPlaying() const
	{
		std::lock_guard<std::mutex> Guard(m_Lock);
		return m_Name;
	}

	float CAudio::Duration() const
	{
		std::lock_guard<std::mutex> Guard(m_Lock);
		return m_Duration;
	}

	float CAudio::Position() const
	{
		// What was played cannot be read back from the decoder easily, so this
		// counts the frames handed to SDL (m_FramesDecoded) and subtracts the
		// frames still waiting in the stream. SDL_GetAudioStreamQueued reports
		// the stream's own F32 stereo bytes, so dividing by two floats per
		// frame gives the queued frames. The result lags the speaker by the
		// device buffer, which is what a progress bar wants anyway.
		std::lock_guard<std::mutex> Guard(m_Lock);
		long long QueuedFrames = 0;
		if(m_pStream != nullptr)
			QueuedFrames = SDL_GetAudioStreamQueued(static_cast<SDL_AudioStream *>(m_pStream)) / BYTES_PER_FRAME;
		const long long Played = std::max(0LL, m_FramesDecoded - QueuedFrames);
		return static_cast<float>(Played) / SAMPLE_RATE;
	}

} // namespace engine

#else // UNITEED_AUDIO

// The headless build does not link SDL3, so it gets a silent CAudio with the
// same interface. CMake never adds this file there, the guard is only here so
// that the translation unit would still compile if it did.

namespace engine
{
	CAudio::CAudio() = default;
	CAudio::~CAudio() { Shutdown(); }
	bool CAudio::Init() { return false; }
	void CAudio::Shutdown() {}
	bool CAudio::Available() const { return false; }
	bool CAudio::Play(std::string_view Name, const storage::byte_vector &Opus)
	{
		(void)Name;
		(void)Opus;
		return false;
	}
	bool CAudio::PlayFile(std::string_view Name, const std::string &Path)
	{
		(void)Name;
		(void)Path;
		return false;
	}
	void CAudio::Stop() {}
	bool CAudio::IsPlaying() const { return false; }
	std::string_view CAudio::NowPlaying() const { return {}; }
	float CAudio::Duration() const { return 0.0f; }
	float CAudio::Position() const { return 0.0f; }
	// without a decoder there is nothing to read the length from
	float OpusDurationSeconds(const storage::byte_vector &Opus)
	{
		(void)Opus;
		return 0.0f;
	}
} // namespace engine

#endif // UNITEED_AUDIO
