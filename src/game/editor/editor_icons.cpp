#include <algorithm>

#include "base/logger.h"
#include "game/editor/editor_icons.h"
#include "game/editor/editor_ui.h"

#if UNITEED_GUI

namespace game::editor_icons
{
	namespace
	{
		// the file name and the cache name of every icon, in enum order
		struct CIconFile
		{
			icon m_Icon;
			const char *m_pName;
			const char *m_pFile;
		};

		const CIconFile g_aIconFiles[] = {
			{icon::pan, "pan", "pan.svg"},
			{icon::brush, "brush", "brush.svg"},
			{icon::bucket, "bucket", "bucket.svg"},
			{icon::eraser, "eraser", "eraser.svg"},
			{icon::marquee, "marquee", "marquee.svg"},
			{icon::frame, "frame", "frame.svg"},
			{icon::grid, "grid", "grid.svg"},
			{icon::keyboard, "keyboard", "keyboard.svg"},
			{icon::toolbox, "toolbox", "toolbox.svg"},
			{icon::layers, "layers", "layers.svg"},
			{icon::tiles, "tiles", "tiles.svg"},
			{icon::assets, "assets", "assets.svg"},
			{icon::envelope, "envelope", "envelope.svg"},
			{icon::rules, "rules", "rules.svg"},
		};

		const CIconFile *Find(icon Icon)
		{
			for(const CIconFile &File : g_aIconFiles)
				if(File.m_Icon == Icon)
					return &File;
			return nullptr;
		}
	} // namespace

	const icon g_aAllIcons[] = {icon::pan, icon::brush, icon::bucket, icon::eraser, icon::marquee, icon::frame,
		icon::grid, icon::keyboard, icon::toolbox, icon::layers, icon::tiles, icon::assets, icon::envelope,
		icon::rules};
	const int g_IconCount = static_cast<int>(std::size(g_aAllIcons));

	const char *Name(icon Icon)
	{
		const CIconFile *pFile = Find(Icon);
		return pFile != nullptr ? pFile->m_pName : "unknown";
	}

	const char *FileName(icon Icon)
	{
		const CIconFile *pFile = Find(Icon);
		return pFile != nullptr ? pFile->m_pFile : "";
	}

	void RegisterAll(engine::CVectorTextures &Textures, const storage::path &Directory)
	{
		int Loaded = 0;
		for(const icon Icon : g_aAllIcons)
		{
			if(Textures.Add(Name(Icon), Directory / FileName(Icon)))
				Loaded++;
		}
		if(Loaded < g_IconCount)
			LOG_WARN("icons", "%d of %d icons were loaded from '%s'", Loaded, g_IconCount,
				Directory.string().c_str());
		else
			LOG_TRACE("icons", "%d icons loaded from '%s'", Loaded, Directory.string().c_str());
	}

	void Draw(engine::CVectorTextures &Textures, ui::CPainter *pPainter, icon Icon, const base::rect &Area,
		ui::CColor Color, float Inset)
	{
		if(pPainter == nullptr || Color.a <= 0.0f)
			return;

		// the square the drawing gets inside the button
		const float Size = std::min(Area.Width(), Area.Height()) - Inset * 2.0f;
		if(Size <= 3.0f)
			return;

		int TextureSize = 0;
		const int Texture = Textures.Texture(Name(Icon), Size, TextureSize);
		if(Texture <= 0 || TextureSize <= 0)
			return;

		const base::vec2 TopLeft(Area.Center().x - Size * 0.5f, Area.Center().y - Size * 0.5f);
		ui::CPaint Paint = editor_ui::ImagePaint(Texture, TopLeft, base::vec2(Size, Size),
			0.0f, 0.0f, static_cast<float>(TextureSize), static_cast<float>(TextureSize),
			TextureSize, TextureSize);
		// white art, themed colour
		Paint.m_Tint = Color;

		pPainter->BeginPath();
		pPainter->Rect(TopLeft.x, TopLeft.y, Size, Size);
		pPainter->FillPaint(Paint);
		pPainter->Fill();
	}

	bool Button(engine::CVectorTextures &Textures, ui::CPainter *pPainter, const engine::CInput &Input,
		const base::rect &Area, icon Icon, bool bActive, bool bEnabled, float Inset)
	{
		const bool bHovered = bEnabled && ui::Hovered(Input, Area);
		ui::CColor Background = ui::Theme.m_Panel;
		if(bActive)
			Background = ui::Theme.m_AccentDim;
		else if(bHovered)
			Background = ui::Theme.m_Hover;

		ui::Rect(pPainter, Area, Background);
		ui::RectOutline(pPainter, Area, bActive ? ui::Theme.m_Accent : ui::Theme.m_Border);
		Draw(Textures, pPainter, Icon, Area, bEnabled ? ui::Theme.m_Text : ui::Theme.m_TextDim, Inset);
		return bEnabled && bHovered && Input.MousePressed(engine::CInput::mouse_button::left);
	}
} // namespace game::editor_icons

#endif // UNITEED_GUI
