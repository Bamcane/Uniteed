#include "editor_app.h"

#include <SDL3/SDL.h>

#include "base/define.h"
#include "base/logger.h"
#include "base/png.h"
#include "config/config.h"
#include "config/config_keys.h"
#include "engine/graphics.h"
#include "game/editor/editor.h"

namespace engine
{
	namespace
	{
		// the renderer hands back a surface, already top down like PNG wants
		bool SaveFramebuffer(SDL_Renderer *pRenderer, const storage::path &FileName, int Width, int Height)
		{
			if(pRenderer == nullptr || Width <= 0 || Height <= 0)
				return false;
			SDL_Surface *pSurface = SDL_RenderReadPixels(pRenderer, nullptr);
			if(pSurface == nullptr)
			{
				LOG_ERROR("editor", "failed to read the frame back: %s", SDL_GetError());
				return false;
			}

			// one converter for the whole image, then a plain conversion
			SDL_Surface *pRgba = SDL_ConvertSurface(pSurface, SDL_PIXELFORMAT_RGBA32);
			SDL_DestroySurface(pSurface);
			if(pRgba == nullptr)
			{
				LOG_ERROR("editor", "failed to convert the frame: %s", SDL_GetError());
				return false;
			}

			const std::size_t Stride = static_cast<std::size_t>(Width) * 4;
			storage::byte_vector Pixels(Stride * static_cast<std::size_t>(Height));
			const std::size_t SourceStride = static_cast<std::size_t>(pRgba->pitch);
			const auto *pSource = static_cast<const std::byte *>(pRgba->pixels);
			for(int y = 0; y < Height; y++)
				std::memcpy(Pixels.data() + static_cast<std::size_t>(y) * Stride,
					pSource + static_cast<std::size_t>(y) * SourceStride, Stride);
			SDL_DestroySurface(pRgba);

			return base::WritePng(FileName, Width, Height, Pixels);
		}
	} // namespace

	int RunEditor(const std::string &MapPath, const std::string &ScreenshotPath)
	{
		const bool bScreenshot = !ScreenshotPath.empty();

		// the settings remember the window size, the brush and the last map
		config::CConfig Config;
		const storage::path ConfigPath = config::CConfig::DefaultPath();
		Config.Load(ConfigPath);
		{
			const std::string Level = Config.Get(config::LogLevel);
			if(Level == "trace")
				base::logger::SetLevel(base::log_level::trace);
			else if(Level == "warn")
				base::logger::SetLevel(base::log_level::warn);
			else if(Level == "error")
				base::logger::SetLevel(base::log_level::error);
			else
				base::logger::SetLevel(base::log_level::info);
		}

		const int WindowWidth = bScreenshot ? 1600 : base::clamp(Config.Get(config::WindowWidth), 640, 7680);
		const int WindowHeight = bScreenshot ? 900 : base::clamp(Config.Get(config::WindowHeight), 480, 4320);

		CGraphics Graphics;
		if(!Graphics.Init(std::string(GAME_DISPLAY_NAME) + " " + GAME_VERSION, WindowWidth, WindowHeight, !bScreenshot))
			return 1;

		game::CEditor Editor;
		Editor.Init(&Graphics);
		Editor.SetGrid(Config.Get(config::ShowGrid));
		Editor.SetBrushSize(Config.Get(config::BrushSize));
		if(const std::optional<game::map_target> Target = game::TargetFromId(Config.Get(config::DefaultTarget)))
			Editor.SetTarget(*Target);

		const std::string ToOpen = MapPath.empty() ? Config.Get(config::LastMap) : MapPath;
		if(!ToOpen.empty())
		{
			if(!Editor.OpenFile(ToOpen))
				LOG_WARN("editor", "could not open '%s', starting empty", ToOpen.c_str());
		}

		if(bScreenshot)
		{
			// the envelope preview would make the picture depend on when it was
			// taken, so the clock is pinned for the screenshot
			Editor.SetEnvelopePlaying(false);
			Editor.SetEnvelopeTime(0.0f);

			// show the most representative state: the first tile layer that has
			// an image, with the brush in the middle of the canvas
			const game::CMap &Map = Editor.Map();
			for(std::size_t g = 0; g < Map.m_vGroups.size(); g++)
			{
				const game::CGroup &Group = Map.m_vGroups[g];
				for(std::size_t l = 0; l < Group.m_vLayers.size(); l++)
				{
					const game::CLayer &Layer = Group.m_vLayers[l];
					if(Layer.IsTiles() && !Layer.IsPhysicsLayer() && Layer.m_Image >= 0)
					{
						Editor.SetSelectedLayer(static_cast<int>(g), static_cast<int>(l));
						Editor.SetCursor(base::vec2(Layer.m_Width * 16.0f, Layer.m_Height * 16.0f));
						goto found;
					}
				}
			}
		found:

			// two frames so the textures created on the first one are uploaded,
			// then read the buffer back
			for(int Frame = 0; Frame < 2; Frame++)
			{
				Graphics.BeginFrame(false);
				Editor.Update();
				// Update() clears the cursor when the real mouse is outside the
				// canvas, so put the demo cursor back before drawing
				if(Frame == 0)
					Editor.SetCursor(base::vec2(800.0f, 800.0f));
				else
					Editor.SetCursor(base::vec2(800.0f, 800.0f));
				Editor.Render();
				Graphics.EndFrame(false);
			}

			// the viewport the frames were drawn with, so the read back covers
			// exactly what is on screen
			const base::vec2 Framebuffer = Graphics.FramebufferSize();
			const int FramebufferWidth = static_cast<int>(Framebuffer.x);
			const int FramebufferHeight = static_cast<int>(Framebuffer.y);
			if(!SaveFramebuffer(Graphics.Renderer(), ScreenshotPath, FramebufferWidth, FramebufferHeight))
			{
				LOG_ERROR("editor", "failed to write the screenshot '%s'", ScreenshotPath.c_str());
				return 1;
			}
			LOG_INFO("editor", "screenshot written to '%s' (%dx%d)", ScreenshotPath.c_str(), FramebufferWidth, FramebufferHeight);
			Graphics.Close();
			return 0;
		}

		while(!Graphics.ShouldQuit())
		{
			Graphics.BeginFrame();
			Editor.Update();
			Editor.Render();
			Graphics.EndFrame();
		}

		// remember the settings for the next start
		const base::vec2 Size = Graphics.WindowSize();
		Config.Set(config::WindowWidth, static_cast<int>(Size.x));
		Config.Set(config::WindowHeight, static_cast<int>(Size.y));
		Config.Set(config::ShowGrid, Editor.Grid());
		Config.Set(config::BrushSize, Editor.BrushSize());
		Config.Set(config::DefaultTarget, std::string(game::GetTargetProfile(Editor.Target()).m_pId));
		Config.Set(config::LastMap, Editor.SourcePath());
		Config.Set(config::AssetVersion, Editor.VersionId());
		Config.Save(ConfigPath);

		Graphics.Close();
		return 0;
	}
} // namespace engine
