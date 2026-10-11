#include "graphics.h"

#include <SDL3/SDL.h>

#include "base/logger.h"
#include "config/config_keys.h"
#include "engine/font_find.h"
#include "game/assets.h"

namespace engine
{
	namespace
	{
		// the drivers to try, in order: an accelerated one first, the software
		// renderer as the last resort
		const char *const g_apDrivers[] = {"opengl", "opengles2", "vulkan", "gpu", nullptr};
	} // namespace

	CGraphics::~CGraphics() { Close(); }

	bool CGraphics::Init(
		const std::string &Title, int Width, int Height, bool Visible, const config::CConfig *pConfig)
	{
		// the editor may ask for the window size before the first frame
		m_WindowSize = base::vec2(static_cast<float>(Width), static_cast<float>(Height));
		m_FramebufferSize = m_WindowSize;

		if(!SDL_Init(SDL_INIT_VIDEO))
		{
			LOG_ERROR("graphics", "failed to initialise SDL: %s", SDL_GetError());
			return false;
		}
		LOG_INFO("graphics", "SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
			SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()));

		SDL_WindowFlags Flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if(!Visible)
			Flags |= SDL_WINDOW_HIDDEN;

		m_pWindow = SDL_CreateWindow(Title.c_str(), Width, Height, Flags);
		if(m_pWindow == nullptr)
		{
			LOG_ERROR("graphics", "failed to create the window: %s", SDL_GetError());
			SDL_Quit();
			return false;
		}

		// below this the two side panels would eat the canvas; it is the same
		// lower bound the stored window size is clamped to
		SDL_SetWindowMinimumSize(m_pWindow, 640, 480);
		// the name fields read what the user types
		SDL_StartTextInput(m_pWindow);

		// the driver list is handed to SDL as a comma separated preference, so
		// a machine without one of them still gets a window
		std::string DriverList;
		for(int i = 0; g_apDrivers[i] != nullptr; i++)
		{
			if(!DriverList.empty())
				DriverList += ",";
			DriverList += g_apDrivers[i];
		}
		m_pRenderer = SDL_CreateRenderer(m_pWindow, DriverList.c_str());
		if(m_pRenderer == nullptr)
		{
			LOG_WARN("graphics", "no accelerated renderer worked, using the software one");
			m_pRenderer = SDL_CreateRenderer(m_pWindow, SDL_SOFTWARE_RENDERER);
		}
		if(m_pRenderer == nullptr)
		{
			LOG_ERROR("graphics", "failed to create a renderer: %s", SDL_GetError());
			Close();
			return false;
		}
		LOG_INFO("graphics", "renderer '%s'", SDL_GetRendererName(m_pRenderer));
		SDL_SetRenderVSync(m_pRenderer, 1);

		// the three system faces, with the configured overrides taking precedence
		fonts::request FontRequest;
		FontRequest.m_Primary = pConfig != nullptr ? pConfig->Get(config::FontPath) : std::string{};
		FontRequest.m_Bold = pConfig != nullptr ? pConfig->Get(config::FontPathBold) : std::string{};
		FontRequest.m_Cjk = pConfig != nullptr ? pConfig->Get(config::FontPathCjk) : std::string{};
		const fonts::result Fonts = fonts::Resolve(FontRequest);

		m_FontPath = Fonts.m_Primary;
		if(!m_Text.Init(Fonts.m_Primary, Fonts.m_Bold, Fonts.m_Cjk))
		{
			LOG_ERROR("graphics", "failed to initialise the text engine");
			Close();
			return false;
		}

		m_Painter.Init(m_pRenderer, &m_Text);
		m_Painter.SetTextureLookup(&CGraphics::LookupTextureCallback, this);

		LOG_INFO("graphics", "window created, font '%s'", m_FontPath.empty() ? "(none)" : m_FontPath.c_str());
		return true;
	}

	void CGraphics::Close()
	{
		ForgetTextures();
		m_Painter.Close();
		m_Text.Close();
		if(m_pRenderer != nullptr)
		{
			SDL_DestroyRenderer(m_pRenderer);
			m_pRenderer = nullptr;
		}
		if(m_pWindow != nullptr)
		{
			SDL_DestroyWindow(m_pWindow);
			m_pWindow = nullptr;
			SDL_Quit();
		}
	}

	bool CGraphics::ShouldQuit() const { return m_bShouldQuit || m_pWindow == nullptr; }
	void CGraphics::RequestQuit() { m_bShouldQuit = true; }

	void CGraphics::HandleEvent(const SDL_Event &Event)
	{
		switch(Event.type)
		{
		case SDL_EVENT_QUIT:
			m_bShouldQuit = true;
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			m_Input.OnKey(static_cast<int>(Event.key.key),
				Event.key.down ? CInput::action_press : CInput::action_release, Event.key.mod);
			break;
		case SDL_EVENT_TEXT_INPUT:
			m_Input.OnText(Event.text.text);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			m_Input.OnMouseButton(Event.button.button,
				Event.button.down ? CInput::action_press : CInput::action_release);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			m_Input.OnCursorPos(Event.motion.x, Event.motion.y);
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			m_Input.OnScroll(Event.wheel.x, Event.wheel.y);
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			// a key could stay stuck otherwise
			m_Input.OnFocusLost();
			break;
		default:
			break;
		}
	}

	void CGraphics::BeginFrame(bool PumpEvents)
	{
		// the per frame buffers are cleared before the events arrive, so the
		// state read below is exactly this frame's input
		m_Input.NewFrame();
		if(PumpEvents)
		{
			SDL_Event Event;
			while(SDL_PollEvent(&Event))
				HandleEvent(Event);
		}

		int WindowWidth = 0, WindowHeight = 0;
		SDL_GetWindowSize(m_pWindow, &WindowWidth, &WindowHeight);
		int FramebufferWidth = 0, FramebufferHeight = 0;
		SDL_GetWindowSizeInPixels(m_pWindow, &FramebufferWidth, &FramebufferHeight);
		m_PixelRatio = (WindowWidth > 0) ? static_cast<float>(FramebufferWidth) / static_cast<float>(WindowWidth) : 1.0f;
		if(m_PixelRatio <= 0.0f)
			m_PixelRatio = 1.0f;

		const base::vec2 WindowSize(static_cast<float>(WindowWidth), static_cast<float>(WindowHeight));
		const base::vec2 FramebufferSize(static_cast<float>(FramebufferWidth), static_cast<float>(FramebufferHeight));
		if(WindowSize.x != m_WindowSize.x || WindowSize.y != m_WindowSize.y ||
			FramebufferSize.x != m_FramebufferSize.x || FramebufferSize.y != m_FramebufferSize.y)
		{
			LOG_TRACE("graphics", "window %.0fx%.0f, framebuffer %.0fx%.0f, pixel ratio %.2f",
				WindowSize.x, WindowSize.y, FramebufferSize.x, FramebufferSize.y, m_PixelRatio);
			m_WindowSize = WindowSize;
			m_FramebufferSize = FramebufferSize;
		}

		SDL_SetRenderDrawColor(m_pRenderer, static_cast<Uint8>(m_ClearColor[0] * 255.0f),
			static_cast<Uint8>(m_ClearColor[1] * 255.0f), static_cast<Uint8>(m_ClearColor[2] * 255.0f), 255);
		SDL_RenderClear(m_pRenderer);

		m_Painter.BeginFrame(m_WindowSize.x, m_WindowSize.y);
	}

	void CGraphics::EndFrame(bool Present)
	{
		m_Painter.EndFrame();
		if(Present)
			SDL_RenderPresent(m_pRenderer);
		m_Input.EndFrame();
	}

	void CGraphics::SetWindowTitle(const std::string &Title)
	{
		if(m_pWindow != nullptr)
			SDL_SetWindowTitle(m_pWindow, Title.c_str());
	}

	void CGraphics::SetWindowSize(int Width, int Height)
	{
		if(m_pWindow != nullptr)
			SDL_SetWindowSize(m_pWindow, Width, Height);
	}

	void CGraphics::SetClearColor(float Red, float Green, float Blue)
	{
		m_ClearColor[0] = Red;
		m_ClearColor[1] = Green;
		m_ClearColor[2] = Blue;
	}

	SDL_Texture *CGraphics::LookupTextureCallback(void *pUser, int Texture)
	{
		// the handle is an index into the graphics texture table
		auto *pGraphics = static_cast<CGraphics *>(pUser);
		if(pGraphics == nullptr || Texture < 1 || Texture > static_cast<int>(pGraphics->m_vTextureHandles.size()))
			return nullptr;
		return pGraphics->m_vTextureHandles[static_cast<std::size_t>(Texture) - 1];
	}

	int CGraphics::Texture(const std::string &Key) const
	{
		if(!m_Textures.contains(Key))
			return -1;
		return m_Textures.at(Key).value();
	}

	int CGraphics::RegisterTexture(const std::string &Key, SDL_Texture *pTexture, base::vec2 Size)
	{
		// handles start at 1; 0 and -1 are the "nothing" values the callers use
		m_vTextureHandles.push_back(pTexture);
		const int Handle = static_cast<int>(m_vTextureHandles.size());
		m_Textures.emplace(Key, Handle);
		m_TextureSizes.emplace(Key, Size);
		return Handle;
	}

	int CGraphics::LoadTextureFile(const std::string &Key, const storage::path &File)
	{
		if(m_Textures.contains(Key))
			return Texture(Key);

		// SDL has no image loader of its own, so the file is read and decoded
		// with the same PNG decoder the map resources go through
		const std::optional<storage::byte_vector> Bytes = storage::read(File);
		if(!Bytes.has_value())
		{
			LOG_WARN("graphics", "failed to read the texture '%s'", File.string().c_str());
			m_Textures.emplace(Key, -1);
			return -1;
		}
		const std::optional<game::CDecodedImage> Decoded = game::DecodePng(*Bytes);
		if(!Decoded.has_value())
		{
			LOG_WARN("graphics", "failed to decode the texture '%s'", File.string().c_str());
			m_Textures.emplace(Key, -1);
			return -1;
		}

		const int Handle = CreateTextureRGBA(Key, Decoded->m_Width, Decoded->m_Height, Decoded->m_Pixels);
		if(Handle > 0)
			LOG_TRACE("graphics", "loaded texture '%s' from '%s' (%dx%d)", Key.c_str(), File.string().c_str(),
				Decoded->m_Width, Decoded->m_Height);
		return Handle;
	}

	int CGraphics::CreateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels)
	{
		if(m_Textures.contains(Key))
			return Texture(Key);
		if(Width <= 0 || Height <= 0 || Pixels.size() < static_cast<std::size_t>(Width) * Height * 4)
			return -1;

		SDL_Texture *pTexture = SDL_CreateTexture(m_pRenderer, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STATIC, Width, Height);
		if(pTexture == nullptr)
		{
			LOG_WARN("graphics", "failed to create the texture '%s': %s", Key.c_str(), SDL_GetError());
			m_Textures.emplace(Key, -1);
			return -1;
		}
		SDL_SetTextureScaleMode(pTexture, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
		// the decoded resources are tightly packed RGBA, which is what the
		// renderer wants for ABGR8888 on a little endian machine
		SDL_UpdateTexture(pTexture, nullptr, Pixels.data(), Width * 4);

		return RegisterTexture(Key, pTexture, base::vec2(static_cast<float>(Width), static_cast<float>(Height)));
	}

	bool CGraphics::UpdateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels)
	{
		if(!m_Textures.contains(Key))
			return false;
		SDL_Texture *pTexture = LookupTextureCallback(this, m_Textures.at(Key).value());
		if(pTexture == nullptr || Width <= 0 || Height <= 0)
			return false;
		if(Pixels.size() < static_cast<std::size_t>(Width) * Height * 4)
			return false;

		float ExistingWidth = 0.0f, ExistingHeight = 0.0f;
		if(!SDL_GetTextureSize(pTexture, &ExistingWidth, &ExistingHeight))
			return false;
		if(static_cast<int>(ExistingWidth) != Width || static_cast<int>(ExistingHeight) != Height)
			return false;

		SDL_UpdateTexture(pTexture, nullptr, Pixels.data(), Width * 4);
		return true;
	}

	void CGraphics::ForgetTexture(const std::string &Key)
	{
		if(!m_Textures.contains(Key))
			return;
		// the handle slot stays in place: dropping it would renumber every
		// handle after it, which the editor is still holding
		const int Handle = m_Textures.at(Key).value();
		if(Handle >= 1 && Handle <= static_cast<int>(m_vTextureHandles.size()))
		{
			if(SDL_Texture *pTexture = m_vTextureHandles[static_cast<std::size_t>(Handle) - 1])
				SDL_DestroyTexture(pTexture);
			m_vTextureHandles[static_cast<std::size_t>(Handle) - 1] = nullptr;
		}
		m_Textures.lazy_remove(Key);
		if(m_TextureSizes.contains(Key))
			m_TextureSizes.lazy_remove(Key);
	}

	void CGraphics::ForgetTextures()
	{
		for(SDL_Texture *pTexture : m_vTextureHandles)
			if(pTexture != nullptr)
				SDL_DestroyTexture(pTexture);
		m_vTextureHandles.clear();
		m_Textures.clear();
		m_TextureSizes.clear();
	}

	bool CGraphics::TextureSize(int Texture, int &Width, int &Height) const
	{
		SDL_Texture *pTexture = LookupTextureCallback(const_cast<CGraphics *>(this), Texture);
		if(pTexture == nullptr)
			return false;
		float TextureWidth = 0.0f, TextureHeight = 0.0f;
		if(!SDL_GetTextureSize(pTexture, &TextureWidth, &TextureHeight))
			return false;
		Width = static_cast<int>(TextureWidth);
		Height = static_cast<int>(TextureHeight);
		return true;
	}

	float CGraphics::TextWidth(float Size, const std::string &Text, bool Bold) const
	{
		return const_cast<CTextEngine &>(m_Text).Layout(Bold ? m_FontBold : m_Font, Size, Text, nullptr);
	}
} // namespace engine
