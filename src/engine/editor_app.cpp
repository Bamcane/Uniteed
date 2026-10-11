#include "editor_app.h"

#include <SDL3/SDL.h>

#include "base/define.h"
#include "base/i18n.h"
#include "base/logger.h"
#include "base/png.h"
#include "config/config.h"
#include "config/config_keys.h"
#include "engine/graphics.h"
#include "engine/keycodes.h"
#include "game/editor/editor.h"

namespace engine
{
	namespace i18n = base::i18n;

	namespace
	{
		/*
		The language of the interface, in the order the choice wins: the command
		line, the settings file, then the locale the session runs in. It is
		settled before the window exists, because the catalogs are read from the
		data directory and no frame of the interface should be built in the
		wrong language - a window that has to redraw itself in another language
		on its first frame is a window that flickers.
	*/
	void SelectLanguage(const config::CConfig &Config, const CScreenshotSetup &Setup)
	{
		const std::string LanguageId = !Setup.m_Language.empty() ? Setup.m_Language :
			(Config.Has(config::Language) ? Config.Get(config::Language) : i18n::SystemLanguageId());
		if(!i18n::SetLanguage(LanguageId))
		{
			LOG_WARN("uniteed", "no catalog for language '{}', the interface stays English", LanguageId);
			i18n::SetLanguage("en");
		}
	}

	// the renderer hands back a surface, already top down like PNG wants
		bool SaveFramebuffer(SDL_Renderer *pRenderer, const storage::path &FileName, int Width, int Height)
		{
			if(pRenderer == nullptr || Width <= 0 || Height <= 0)
				return false;
			SDL_Surface *pSurface = SDL_RenderReadPixels(pRenderer, nullptr);
			if(pSurface == nullptr)
			{
				LOG_ERROR("editor", "failed to read the frame back: {}", SDL_GetError());
				return false;
			}

			// one converter for the whole image, then a plain conversion
			SDL_Surface *pRgba = SDL_ConvertSurface(pSurface, SDL_PIXELFORMAT_RGBA32);
			SDL_DestroySurface(pSurface);
			if(pRgba == nullptr)
			{
				LOG_ERROR("editor", "failed to convert the frame: {}", SDL_GetError());
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

	void ApplyScreenshotSetup(game::CEditor &Editor, const CScreenshotSetup &Setup)
	{
		const game::CMap &Map = Editor.Map();
		int Group = Setup.m_Group;
		int Layer = Setup.m_Layer;

		// no explicit layer: take the first tile layer that has an image, which
		// is the most representative state of a map
		if(Group < 0 || Layer < 0)
		{
			Group = 0;
			Layer = 0;
			for(std::size_t g = 0; g < Map.m_vGroups.size(); g++)
			{
				const game::CGroup &Candidate = Map.m_vGroups[g];
				bool bFound = false;
				for(std::size_t l = 0; l < Candidate.m_vLayers.size(); l++)
				{
					const game::CLayer &Layer2 = Candidate.m_vLayers[l];
					// the game layer is the most interesting one to show when a
					// scripted run does not say which layer it wants
					if(Layer2.IsGameLayer())
					{
						Group = static_cast<int>(g);
						Layer = static_cast<int>(l);
						bFound = true;
						break;
					}
					if(Layer2.IsTiles() && !Layer2.IsPhysicsLayer() && Layer2.m_Image >= 0)
					{
						Group = static_cast<int>(g);
						Layer = static_cast<int>(l);
						bFound = true;
						break;
					}
				}
				if(bFound)
					break;
			}
		}

		Editor.SetSelectedLayer(Group, Layer);
		Editor.SetCursor(base::vec2(800.0f, 800.0f));
		if(Setup.m_EnvelopeTime >= 0.0f)
			Editor.SetEnvelopeTime(Setup.m_EnvelopeTime);

		// the quad editor is what a screenshot of a quad layer is for, so one
		// quad is selected unless the caller asked for none
		if(Setup.m_Quad != -2 && Group >= 0 && Group < static_cast<int>(Map.m_vGroups.size()))
		{
			const game::CGroup &Candidate = Map.m_vGroups[static_cast<std::size_t>(Group)];
			if(Layer >= 0 && Layer < static_cast<int>(Candidate.m_vLayers.size()))
			{
				const game::CLayer &Layer2 = Candidate.m_vLayers[static_cast<std::size_t>(Layer)];
				if(Layer2.IsQuads() && !Layer2.m_vQuads.empty())
					Editor.SelectQuad(Setup.m_Quad >= 0 ? Setup.m_Quad : 0);
			}
		}

		/*
			The dock tabs, applied last so a setup can pick a workspace and then
			select the layer that workspace is about.
		*/
		const auto DockFromName = [](const std::string &Name) -> int {
			if(Name == "layers") return static_cast<int>(game::CEditor::dock_panel::layers);
			if(Name == "tiles") return static_cast<int>(game::CEditor::dock_panel::tiles);
			if(Name == "envelopes" || Name == "envelope") return static_cast<int>(game::CEditor::dock_panel::envelopes);
			if(Name == "rules") return static_cast<int>(game::CEditor::dock_panel::rules);
			if(Name == "assets" || Name == "asset") return static_cast<int>(game::CEditor::dock_panel::assets);
			return -1;
		};
		/*
			--dock LEFT:RIGHT names the upper and the lower docker, which is the
			order they are read in on the command line; the old left/right
			spelling still means the same two areas, because that is what a
			screenshot script written before the layout change passes.
		*/
		const int UpperDock = DockFromName(Setup.m_LeftDock);
		if(UpperDock >= 0)
			Editor.SetDockTab(game::dock_slot::right_upper, static_cast<game::CEditor::dock_panel>(UpperDock));
		const int LowerDock = DockFromName(Setup.m_RightDock);
		if(LowerDock >= 0)
			Editor.SetDockTab(game::dock_slot::right_lower, static_cast<game::CEditor::dock_panel>(LowerDock));
		if(Setup.m_Envelope >= 0)
			Editor.SetSelectedEnvelope(Setup.m_Envelope);
		if(Setup.m_EnvelopePoint >= 0)
			Editor.SetSelectedEnvelopePoint(Setup.m_EnvelopePoint);

		if(Setup.m_Corner >= 0)
			Editor.SetQuadColorTarget(Setup.m_Corner);
		if(Setup.m_bOpenColorPicker)
			Editor.OpenColorPicker();
		if(Setup.m_Menu == "files")
			Editor.OpenMenu(game::CEditor::menu_id::files);
		else if(Setup.m_Menu == "settings")
			Editor.OpenMenu(game::CEditor::menu_id::settings);
	}

	/*
		Pushes one scripted frame into SDL's event queue, which BeginFrame then
		pumps. The editor therefore sees exactly what a mouse and a keyboard
		would produce: this is a real interaction, not a call into the state.
	*/
	void PushInputFrame(const CInputStep &Step, base::vec2 &Cursor, std::vector<SDL_Keycode> &ModifierKeys)
	{
		const auto Push = [](SDL_Event &Event) { SDL_PushEvent(&Event); };
		/*
			A modifier is a real key press of its own: the editor reads the
			modifier state from the same key event that carries the letter, and
			SDL only fills key.mod when the modifier key is genuinely down.
		*/
		const auto PressKey = [&](SDL_Keycode Key, SDL_Keymod Mods, bool bDown) {
			SDL_Event Event{};
			Event.type = bDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			Event.key.key = Key;
			Event.key.down = bDown;
			Event.key.mod = Mods;
			Push(Event);
		};
		if(Step.m_Modifiers != 0)
		{
			const SDL_Keymod Mods = static_cast<SDL_Keymod>(Step.m_Modifiers);
			if((Step.m_Modifiers & CInput::mod_ctrl) != 0)
			{
				PressKey(engine::keys::LCtrl, Mods, true);
				ModifierKeys.push_back(engine::keys::LCtrl);
			}
			if((Step.m_Modifiers & CInput::mod_shift) != 0)
			{
				PressKey(engine::keys::LShift, Mods, true);
				ModifierKeys.push_back(engine::keys::LShift);
			}
			if((Step.m_Modifiers & CInput::mod_alt) != 0)
			{
				PressKey(engine::keys::LAlt, Mods, true);
				ModifierKeys.push_back(engine::keys::LAlt);
			}
		}
		/*
			A letter that goes with a modifier is pushed down and left down: the
			editor reads KeyDown for the modifier and KeyPressed for the letter
			in the same frame, so releasing the letter here would hide it.
		*/
		if(Step.m_Move.x >= 0.0f)
		{
			Cursor = Step.m_Move;
			SDL_Event Event{};
			Event.type = SDL_EVENT_MOUSE_MOTION;
			Event.motion.x = Cursor.x;
			Event.motion.y = Cursor.y;
			Push(Event);
		}
		if(Step.m_PressButton != 0)
		{
			SDL_Event Event{};
			Event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			Event.button.button = static_cast<std::uint8_t>(Step.m_PressButton);
			Event.button.down = true;
			Event.button.clicks = static_cast<std::uint8_t>(Step.m_Clicks > 0 ? Step.m_Clicks : 1);
			Event.button.x = Cursor.x;
			Event.button.y = Cursor.y;
			Push(Event);
		}
		if(!Step.m_TypeText.empty())
		{
			/*
				What SDL sends for a key that produces text: the fields in the
				interface read the string, not the keycode. SDL 3 keeps the text
				behind a pointer it does not own, so the script holds the string
				itself - the event is consumed on the next frame and the step
				after it is pushed only when that one has been read.
			*/
			static std::string s_TypeText;
			s_TypeText = Step.m_TypeText;
			SDL_Event Event{};
			Event.type = SDL_EVENT_TEXT_INPUT;
			Event.text.text = s_TypeText.c_str();
			Push(Event);
		}
		if(Step.m_ReleaseButton != 0)
		{
			SDL_Event Event{};
			Event.type = SDL_EVENT_MOUSE_BUTTON_UP;
			Event.button.button = static_cast<std::uint8_t>(Step.m_ReleaseButton);
			Event.button.down = false;
			Event.button.x = Cursor.x;
			Event.button.y = Cursor.y;
			Push(Event);
		}
		if(Step.m_PressKey != 0)
		{
			SDL_Event Event{};
			Event.type = SDL_EVENT_KEY_DOWN;
			Event.key.key = static_cast<SDL_Keycode>(Step.m_PressKey);
			Event.key.down = true;
			Event.key.mod = static_cast<SDL_Keymod>(Step.m_Modifiers);
			Push(Event);
		}
		if(Step.m_ReleaseKey != 0)
		{
			SDL_Event Event{};
			Event.type = SDL_EVENT_KEY_UP;
			Event.key.key = static_cast<SDL_Keycode>(Step.m_ReleaseKey);
			Event.key.down = false;
			Push(Event);
		}
		if(Step.m_Scroll != 0)
		{
			SDL_Event Event{};
			Event.type = SDL_EVENT_MOUSE_WHEEL;
			Event.wheel.y = static_cast<float>(Step.m_Scroll);
			Push(Event);
		}
	}

	int RunEditor(const std::string &MapPath, const std::string &ScreenshotPath, const CScreenshotSetup &Setup)
	{
		const bool bScreenshot = !ScreenshotPath.empty();


		// the settings remember the window size, the brush and the last map
		config::CConfig Config;
		const storage::path ConfigPath = config::CConfig::DefaultPath();
		Config.Load(ConfigPath);
		SelectLanguage(Config, Setup);
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

		// the command line wins over the stored setting, and the window has to
		// know before it is created: changing the decorations afterwards is
		// something some platforms only do by recreating the window
		const bool bStoredDecorations = Config.Get(config::ClientDecorations);
		if(Setup.m_ClientDecorations >= 0)
			Config.Set(config::ClientDecorations, Setup.m_ClientDecorations != 0);

		/*
			The stored height is the size of everything below the title bar.
			With a system title bar that is the client area as it is; with a
			drawn one the menu bar lives *inside* the window, where the system
			bar used to be outside it, so the window is that much taller and
			nothing under the bar moves. The screenshot mode picks its own size
			and never remembers it, so it is left alone.
		*/
		const bool bDrawnBar = !bScreenshot && Config.Get(config::ClientDecorations);
		const int MenuBar = static_cast<int>(game::MenuBarHeight);
		const int WindowWidth = bScreenshot ? 1600 : base::clamp(Config.Get(config::WindowWidth), 640, 7680);
		const int WindowHeight = (bScreenshot ? 900 : base::clamp(Config.Get(config::WindowHeight), 480, 4320)) +
			(bDrawnBar ? MenuBar : 0);

		CGraphics Graphics;
		if(!Graphics.Init(std::string(GAME_DISPLAY_NAME) + " " + GAME_VERSION, WindowWidth, WindowHeight, !bScreenshot, &Config))
			return 1;
		// a platform that kept its decorations draws no menu bar either, so the
		// height that was reserved for it comes back off
		if(bDrawnBar && !Graphics.ClientDecorations())
			Graphics.SetWindowSize(WindowWidth, WindowHeight - MenuBar);
		// and a session that ended maximized opens that way, at the size it will
		// return to when it is put back
		if(!bScreenshot)
			Graphics.SetMaximized(Config.Get(config::WindowMaximized));
		/*
			The scale the vector drawings are rasterized at, set before the
			editor exists: the editor parses the icons once at start up, so the
			scale has to be known by then. The screenshot setup can override the
			config, which is how a test asks for the same icons at another
			resolution without a second monitor.
		*/
		Graphics.SetUiScale(Setup.m_UiScale > 0.0f ? Setup.m_UiScale : Config.Get(config::UiScale));

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
				LOG_WARN("editor", "could not open '{}', starting empty", ToOpen.c_str());
		}

		if(bScreenshot)
		{
			// the envelope preview would make the picture depend on when it was
			// taken, so the clock is pinned for the screenshot
			Editor.SetEnvelopePlaying(false);
			Editor.SetEnvelopeTime(Setup.m_EnvelopeTime >= 0.0f ? Setup.m_EnvelopeTime : 0.0f);
			ApplyScreenshotSetup(Editor, Setup);

			// two frames so the textures created on the first one are uploaded,
			// then read the buffer back
			for(int Frame = 0; Frame < 2; Frame++)
			{
				/*
					The scale is raised between the two frames when the caller
					asked for it, so the icons are rasterized once for the
					ordinary display and then again for the denser one. That is
					the path a window dragged to another monitor takes, and it
					is what the picture then shows.
				*/
				if(Frame == 1 && Setup.m_UiScaleChange > 0.0f)
					Graphics.SetUiScale(Setup.m_UiScaleChange);
				// and a second map is opened the same way, which throws every
				// texture of the first one away
				if(Frame == 1 && !Setup.m_OpenAfter.empty() && !Editor.OpenFile(Setup.m_OpenAfter))
					LOG_WARN("editor", "could not open '{}'", Setup.m_OpenAfter.c_str());
				Graphics.BeginFrame(false);
				Editor.Update();
				// Update() clears the cursor when the real mouse is outside the
				// canvas, so put the demo cursor back before drawing
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
				LOG_ERROR("editor", "failed to write the screenshot '{}'", ScreenshotPath.c_str());
				return 1;
			}
			LOG_INFO("editor", "screenshot written to '{}' ({:d}x{:d})", ScreenshotPath.c_str(), FramebufferWidth, FramebufferHeight);
			// the audio device has to be closed before SDL_Quit(), which
			// Graphics.Close() calls: SDL_Quit would tear the audio subsystem
			// down underneath a live stream
			Editor.ShutdownSoundPreview();
			Graphics.Close();
			return 0;
		}

		int Frames = 0;
		while(!Graphics.ShouldQuit())
		{
			Graphics.BeginFrame();
			Editor.Update();
			Editor.Render();
			Graphics.EndFrame();
			/*
				A frame budget. It is how a test drives a session all the way to
				its end - the settings are written on the way out - without a
				hand on the mouse, which is the only way to check what a quit
				remembers.
			*/
			if(Setup.m_QuitAfter > 0 && ++Frames >= Setup.m_QuitAfter)
				break;
		}

		/*
			The geometry is remembered as the *resting* size plus whether the
			window was maximized, so a session that ended maximized opens
			maximized again instead of the window growing every restart. The
			height is stored the way it was read: everything below the title
			bar, which on a drawn bar means taking the menu bar back off.
		*/
		const base::vec2 Size = Graphics.RestingSize();
		Config.Set(config::WindowWidth, static_cast<int>(Size.x));
		Config.Set(config::WindowHeight,
			static_cast<int>(Size.y) - (Graphics.ClientDecorations() ? MenuBar : 0));
		Config.Set(config::WindowMaximized, Graphics.Maximized());
		Config.Set(config::ShowGrid, Editor.Grid());
		Config.Set(config::BrushSize, Editor.BrushSize());
		Config.Set(config::DefaultTarget, std::string(game::GetTargetProfile(Editor.Target()).m_pId));
		Config.Set(config::LastMap, Editor.SourcePath());
		Config.Set(config::AssetVersion, Editor.VersionId());
		/*
			The language is the i18n module's state rather than the editor's, so
			a session that was switched to Chinese through the menu comes back
			in Chinese. `--language` is a choice for this run, the way
			`--titlebar` is: what the settings file says is left alone, because
			a flag a script passes must not become somebody's setting.
		*/
		if(Setup.m_Language.empty())
			Config.Set(config::Language, i18n::CurrentId());
		/*
			`--titlebar` is a choice for this run, like `--ui-scale`: what the
			settings file says is put back before it is written, so the flag
			does not quietly become the setting. The persistent way to ask for
			the system title bar is `uniteed_client_decorations` in this file.
		*/
		Config.Set(config::ClientDecorations, bStoredDecorations);
		Config.Save(ConfigPath);

		// the audio device belongs to SDL, which Graphics.Close() quits
		Editor.ShutdownSoundPreview();
		Graphics.Close();
		return 0;
	}

	int RunEditorScript(const std::string &MapPath, const std::string &ScreenshotPath,
		const CScreenshotSetup &Setup, const std::vector<CInputStep> &Steps)
	{
		config::CConfig Config;
		Config.Load(config::CConfig::DefaultPath());
		if(Setup.m_ClientDecorations >= 0)
			Config.Set(config::ClientDecorations, Setup.m_ClientDecorations != 0);

		SelectLanguage(Config, Setup);

		CGraphics Graphics;
		if(!Graphics.Init(std::string(GAME_DISPLAY_NAME) + " " + GAME_VERSION, 1600, 900, false, &Config))
			return 1;
		Graphics.SetUiScale(Setup.m_UiScale > 0.0f ? Setup.m_UiScale : Config.Get(config::UiScale));

		game::CEditor Editor;
		Editor.Init(&Graphics);
		// the envelope clock is pinned: a colour or position envelope must not
		// make the result depend on when the script ran
		Editor.SetEnvelopePlaying(false);
		Editor.SetEnvelopeTime(Setup.m_EnvelopeTime >= 0.0f ? Setup.m_EnvelopeTime : 0.0f);

		if(!MapPath.empty() && !Editor.OpenFile(MapPath))
		{
			LOG_ERROR("editor", "could not open '{}'", MapPath.c_str());
			Graphics.Close();
			return 1;
		}
		ApplyScreenshotSetup(Editor, Setup);

		// one warm up frame so the textures created while drawing are uploaded
		// before the script starts
		Graphics.BeginFrame(false);
		Editor.Update();
		Editor.Render();
		Graphics.EndFrame(false);

		// the density the script runs at, raised after the warm up frame so it
		// behaves exactly like the plain screenshot mode
		if(Setup.m_UiScaleChange > 0.0f)
			Graphics.SetUiScale(Setup.m_UiScaleChange);
		// and another map opened after it, which is the other thing that throws
		// every texture away
		if(!Setup.m_OpenAfter.empty() && !Editor.OpenFile(Setup.m_OpenAfter))
			LOG_WARN("editor", "could not open '{}'", Setup.m_OpenAfter.c_str());

		base::vec2 Cursor(1.0f, 1.0f);
		std::vector<SDL_Keycode> ModifierKeys;
		for(const CInputStep &Step : Steps)
		{
			/*
				A step aimed at a point of a quad asks the editor where that point
				is on this frame: the corner of a map quad is a world position, and
				the pixel it lands on depends on the view and the layout.
			*/
			CInputStep Resolved = Step;
			if(Step.m_QuadPoint.x >= 0)
			{
				Resolved.m_Move = Editor.QuadPointScreen(Step.m_QuadPoint.x, Step.m_QuadPoint.y);
				if(Resolved.m_Move.x < 0.0f)
					LOG_WARN("editor", "quad {:d} has no point {:d} to click", Step.m_QuadPoint.x, Step.m_QuadPoint.y);
			}
			PushInputFrame(Resolved, Cursor, ModifierKeys);
			Graphics.BeginFrame(true);
			Editor.Update();
			Editor.Render();
			Graphics.EndFrame(false);

			/*
				The modifiers are released on the frame after the one that used
				them: the editor reads the keyboard once per frame, so releasing
				them in the same frame would hide the press from it. The frame
				is drawn like any other, because it can be the last one and the
				picture would otherwise be the cleared background.
			*/
			if(!ModifierKeys.empty())
			{
				Graphics.BeginFrame(false);
				for(const SDL_Keycode Key : ModifierKeys)
				{
					SDL_Event Event{};
					Event.type = SDL_EVENT_KEY_UP;
					Event.key.key = Key;
					Event.key.down = false;
					Event.key.mod = SDL_KMOD_NONE;
					SDL_PushEvent(&Event);
				}
				Editor.Update();
				Editor.Render();
				Graphics.EndFrame(false);
				ModifierKeys.clear();
			}
		}

		const base::vec2 Framebuffer = Graphics.FramebufferSize();
		const int FramebufferWidth = static_cast<int>(Framebuffer.x);
		const int FramebufferHeight = static_cast<int>(Framebuffer.y);
		if(!SaveFramebuffer(Graphics.Renderer(), ScreenshotPath, FramebufferWidth, FramebufferHeight))
		{
			LOG_ERROR("editor", "failed to write the screenshot '{}'", ScreenshotPath.c_str());
			Editor.ShutdownSoundPreview();
			Graphics.Close();
			return 1;
		}
		LOG_INFO("editor", "script of {:d} steps written to '{}' ({:d}x{:d})", Steps.size(), ScreenshotPath.c_str(), FramebufferWidth, FramebufferHeight);
		Editor.ShutdownSoundPreview();
		Graphics.Close();
		return 0;
	}
} // namespace engine
