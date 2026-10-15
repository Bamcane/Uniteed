/*
	The asset browser: the images and the sounds a map carries.

	This is the panel the DDRaceNetwork editor is built around, because a map
	maker spends most of their time here: dropping in a tileset, embedding it so
	the map travels on its own, replacing one that changed, or picking the sound
	a sound layer plays. A list of names would not do; the image has to be
	visible, which is why every entry draws a thumbnail of the real pixels.
*/

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "base/string.h"
#include "engine/graphics.h"
#include "engine/ui.h"
#include "game/assets.h"
#include "game/editor/editor.h"
#include "game/editor/editor_ui.h"

#if UNITEED_GUI

namespace game
{
	using base::i18n::Tr;
	namespace ui = engine::ui;

	namespace
	{
		constexpr float AssetRowHeight = 26.0f;
		constexpr float ThumbSize = 56.0f;

		std::string TrimmedName(std::string_view Name)
		{
			// the map format stores a name in 11 usable bytes; showing the
			// truncation makes a name that will not survive a save visible here
			return Name.size() <= 11 ? std::string(Name) : std::string(Name.substr(0, 11)) + "~";
		}
	} // namespace

	int CEditor::AssetThumbnail(int ImageIndex)
	{
		if(m_pGraphics == nullptr)
			return -1;
		if(ImageIndex < 0 || ImageIndex >= ImageCount())
			return -1;
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];

		const std::string Key = "thumb:" + Image.m_Name;
		if(m_pGraphics->Texture(Key) != -1)
			return m_pGraphics->Texture(Key);

		// an embedded image already has its pixels; an external one is decoded
		// from its mapres file, and both end up as the same texture
		if(Image.IsEmbedded())
			return m_pGraphics->CreateTextureRGBA(Key, Image.m_Width, Image.m_Height, Image.m_Data);

		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
			return -1;
		const std::optional<byte_vector> Bytes = storage::read(*File);
		if(!Bytes.has_value())
			return -1;
		const std::optional<CDecodedImage> Decoded = DecodePng(*Bytes);
		if(!Decoded.has_value())
			return -1;
		return m_pGraphics->CreateTextureRGBA(Key, Decoded->m_Width, Decoded->m_Height, Decoded->m_Pixels);
	}

	void CEditor::DrawAssetsPanel(engine::painter::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = this->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y),
			base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);
		if(Area.Width() <= 60.0f || Area.Height() <= 80.0f)
			return;

		const float X = Area.m_TopLeft.x + 8.0f;
		const float Width = Area.Width() - 16.0f;
		float y = Area.m_TopLeft.y + 6.0f;

		// the clamped selections, so a removed asset cannot leave the browser
		// pointing past the end of its list
		if(m_SelectedImage >= ImageCount())
			m_SelectedImage = ImageCount() - 1;
		if(m_SelectedSound >= SoundCount())
			m_SelectedSound = SoundCount() - 1;

		// ------------------------------------------------------------- header
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(X, y + 13.0f), 13.0f, ui::Theme.m_Text, Tr("Assets") );
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 8.0f, y + 13.0f), 11.5f,
			ui::Theme.m_TextDim,   base::FormatRuntime(Tr("{:d} img  {:d} snd"), ImageCount(), SoundCount())  , ui::text_align::right);
		y += 24.0f;

		const auto Row = [&](const char *pLabel, const std::string &Detail, float RowWidth) {
			const base::rect Rect = base::rect::FromSize(base::vec2(X, y), base::vec2(RowWidth, 22.0f));
			if(ui::Hovered(Input, Rect))
				Hint(Rect, pLabel, Detail);
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, pLabel, false);
		};

		// ------------------------------------------------------------ toolbar
		{
			const float ButtonWidth = std::max(48.0f, (Width - 12.0f) / 4.0f);
			float bx = X;
			if(Row(Tr("Import"), Tr("bring a PNG in as an image of the map, through the system dialog"), ButtonWidth))
				ImportImageDialog();
			bx += ButtonWidth + 4.0f;
			// the two import kinds share the first row, so the labels say which
			// one each button is
			{
				const base::rect Rect = base::rect::FromSize(base::vec2(bx, y), base::vec2(ButtonWidth, 22.0f));
				if(ui::Hovered(Input, Rect))
					Hint(Rect, Tr("Sound") , Tr("bring an .opus in as a sound of the map, through the system dialog") );
				if(ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Tr("Sound") , false))
					ImportSoundDialog();
			}
			bx += ButtonWidth + 4.0f;
			{
				const base::rect Rect = base::rect::FromSize(base::vec2(bx, y), base::vec2(ButtonWidth, 22.0f));
				const bool bEnabled = m_SelectedImage >= 0 || m_SelectedSound >= 0;
				if(ui::Hovered(Input, Rect))
					Hint(Rect, Tr("Export") ,  bEnabled ? Tr("write the selected asset out as a file") :
						Tr("select an image or a sound first") );
				if(ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Tr("Export") , false, bEnabled))
					ExportAssetDialog(m_SelectedSound >= 0 && m_SelectedImage < 0);
			}
			bx += ButtonWidth + 4.0f;
			{
				const base::rect Rect = base::rect::FromSize(base::vec2(bx, y), base::vec2(ButtonWidth, 22.0f));
				const bool bEnabled = m_SelectedImage >= 0 || m_SelectedSound >= 0;
				if(ui::Hovered(Input, Rect))
					Hint(Rect, Tr("Remove") ,  bEnabled ? Tr("drop the selected asset, which renumbers what used it") :
						Tr("select an image or a sound first") );
				if(ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Tr("Remove") , false, bEnabled))
				{
					std::string Error;
					if(m_SelectedImage >= 0)
					{
						if(RemoveImage(m_SelectedImage, &Error))
							m_SelectedImage = std::min(m_SelectedImage, ImageCount() - 1);
					}
					else if(RemoveSound(m_SelectedSound, &Error))
						m_SelectedSound = std::min(m_SelectedSound, SoundCount() - 1);
					if(!Error.empty())
						Notify(Error);
				}
			}
			y += 28.0f;
		}

		// ------------------------------------------------------------- images
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(X, y + 12.0f), 12.5f, ui::Theme.m_Text, Tr("Images") );
		y += 20.0f;

		for(int i = 0; i < ImageCount(); i++)
		{
			if(y + AssetRowHeight > Area.m_BottomRight.y - 120.0f)
				break;
			const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(i)];
			const base::rect Entry = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, AssetRowHeight));
			const bool bHover = ui::Hovered(Input, Entry);
			const bool bActive = i == m_SelectedImage;
			if(bHover)
				Hint(Entry, Image.m_Name,   base::FormatRuntime(Tr("image {:d}: {:d}x{:d}, {}, used by {:d} layers"), i, Image.m_Width,
					Image.m_Height, Image.m_External ? Tr("a file next to the map") : Tr("embedded in the map"),
					ImageReferenceCount(i))  );
			if(bActive)
				ui::Rect(pPainter, Entry, ui::Theme.m_Selection);
			else if(bHover)
				ui::Rect(pPainter, Entry, ui::Theme.m_Hover);

			const int Texture = AssetThumbnail(i);
			const float IconSize = AssetRowHeight - 6.0f;
			const base::rect Icon = base::rect::FromSize(base::vec2(Entry.m_TopLeft.x + 3.0f, Entry.m_TopLeft.y + 3.0f),
				base::vec2(IconSize, IconSize));
			if(Texture != -1)
			{
				const ui::CPaint Paint = editor_ui::ImagePaint(Texture, Icon.m_TopLeft, base::vec2(IconSize, IconSize),
					0.0f, 0.0f, static_cast<float>(Image.m_Width), static_cast<float>(Image.m_Height),
					Image.m_Width, Image.m_Height, 1.0f);
				pPainter->BeginPath();
				pPainter->Rect(Icon.m_TopLeft.x, Icon.m_TopLeft.y, IconSize, IconSize);
				pPainter->FillPaint(Paint);
				pPainter->Fill();
			}
			else
				ui::Rect(pPainter, Icon, ui::Theme.m_PanelDark);

			// the index is a badge of its own, so a two digit one cannot run
			// into the name
			const std::string Label = base::Format("{:d}", i);
			const float Badge = 22.0f;
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Entry.m_TopLeft.x + IconSize + 8.0f, Entry.m_TopLeft.y + 11.0f),
				11.5f, ui::Theme.m_TextDim, Label);
			const std::string NameText = TrimmedName(Image.m_Name);
			ui::TextClipped(pPainter, m_pGraphics->Font(),
				base::vec2(Entry.m_TopLeft.x + IconSize + 8.0f + Badge, Entry.m_TopLeft.y + 11.0f),
				11.5f, ui::Theme.m_Text, NameText, Width - IconSize - 12.0f - Badge);
			const std::string Detail = base::Format("{:d}x{:d} {}  {:d} use{}", Image.m_Width, Image.m_Height,
				Image.IsEmbedded() ? "embedded" : "external", ImageReferenceCount(i),
				ImageReferenceCount(i) == 1 ? "" : "s");
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Entry.m_TopLeft.x + IconSize + 22.0f, Entry.m_TopLeft.y + 23.0f),
				10.5f, ui::Theme.m_TextDim, Detail, Width - IconSize - 28.0f);

			if(bHover && Input.MousePressed(engine::CInput::mouse_button::left))
			{
				m_SelectedImage = i;
				m_SelectedSound = -1;
			}
			y += AssetRowHeight + 2.0f;
		}

		// a detail strip for the selected image: the operations that need to
		// explain themselves (embedding, which changes what the map carries)
		if(m_SelectedImage >= 0 && m_SelectedImage < ImageCount() && y + 26.0f < Area.m_BottomRight.y - 100.0f)
		{
			float bx = X;
			const float ButtonWidth = std::max(50.0f, (Width - 8.0f) / 3.0f);
			std::string Error;
			auto AssetButton = [&](const char *pLabel, const std::string &Detail) {
				const base::rect Rect = base::rect::FromSize(base::vec2(bx, y), base::vec2(ButtonWidth, 22.0f));
				if(ui::Hovered(Input, Rect))
					Hint(Rect, pLabel, Detail);
				const bool bClicked = ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, pLabel);
				bx += ButtonWidth + 4.0f;
				return bClicked;
			};
			const bool bEmbedded = m_Map.m_vImages[static_cast<std::size_t>(m_SelectedImage)].IsEmbedded();
			if(AssetButton(bEmbedded ? Tr("Free") : Tr("Embed"), bEmbedded ?
				Tr("let the image live as a file the map names, and drop the copy inside the project") :
				Tr("copy the image into the map file, so the map travels on its own")))
			{
				if(bEmbedded)
				{
					if(!ExternaliseImage(m_SelectedImage, &Error))
						Notify(Error);
				}
				else if(!EmbedImage(m_SelectedImage, &Error))
					Notify(Error);
			}
			if(AssetButton(Tr("Replace"), Tr("read new pixels into this image, keeping its name and the layers that use it")))
			{
				// the platform dialog, so the new pixels come from the file the
				// map maker picked rather than from a path typed into a box
				m_FileDialogAsset = m_SelectedImage;
				if(m_pGraphics != nullptr && m_pGraphics->Window() != nullptr)
				{
					std::vector<engine::CFileFilter> Filters;
					Filters.push_back({"Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.gif"});
					Filters.push_back({"every file", "*"});
					if(m_FileDialog.Open(engine::dialog_kind::open_file, m_pGraphics->Window(), "Replace the image",
						storage::GetProjectsDirectory().string(), std::string(), Filters, false))
						m_FileDialogPurpose = file_dialog_purpose::import_image;
				}
			}
			if(AssetButton(Tr("Export"), Tr("write this image out as a PNG")))
				ExportAssetDialog(false);
			y += 26.0f;
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 10.0f), 10.5f, ui::Theme.m_TextDim,  
				base::FormatRuntime(Tr("image {:d}: {}"), m_SelectedImage, m_Map.m_vImages[static_cast<std::size_t>(m_SelectedImage)].m_Name.c_str())  ,
				Width);
			y += 18.0f;
		}

		// ------------------------------------------------------------- sounds
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(X, y + 12.0f), 12.5f, ui::Theme.m_Text, Tr("Sounds") );
		y += 20.0f;
		for(int i = 0; i < SoundCount(); i++)
		{
			if(y + AssetRowHeight > Area.m_BottomRight.y - 40.0f)
				break;
			const CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(i)];
			const base::rect Entry = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, AssetRowHeight));
			const bool bHover = ui::Hovered(Input, Entry);
			const bool bActive = i == m_SelectedSound;
			if(bActive)
				ui::Rect(pPainter, Entry, ui::Theme.m_Selection);
			else if(bHover)
				ui::Rect(pPainter, Entry, ui::Theme.m_Hover);

			const float Length = SoundLength(i);
			const std::string Detail = base::Format("{}  {}  {:d} use{}", Sound.m_External != 0 ? "external" : "embedded",
				Length > 0.0f ? base::Format("{:.2f}s", static_cast<double>(Length)).c_str() : "length ?",
				SoundReferenceCount(i), SoundReferenceCount(i) == 1 ? "" : "s");
			if(bHover)
				Hint(Entry,   Sound.m_Name.empty() ? base::FormatRuntime(Tr("sound {:d}"), i) : Sound.m_Name  ,  
					base::FormatRuntime(Tr("sound {:d}: {}, {:d} use{} - click twice to hear it"), i,
						Length > 0.0f ? base::FormatRuntime(Tr("{:.2f}s"), static_cast<double>(Length)).c_str() : Tr("length unknown"),
						SoundReferenceCount(i), SoundReferenceCount(i) == 1 ? Tr("") : Tr("s"))  );
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Entry.m_TopLeft.x + 6.0f, Entry.m_TopLeft.y + 11.0f),
				11.5f, ui::Theme.m_TextDim,   base::FormatRuntime(Tr("{:d}"), i)  );
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Entry.m_TopLeft.x + 28.0f, Entry.m_TopLeft.y + 11.0f),
				11.5f, ui::Theme.m_Text, TrimmedName(Sound.m_Name), Width - 34.0f);
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Entry.m_TopLeft.x + 6.0f, Entry.m_TopLeft.y + 23.0f),
				10.5f, ui::Theme.m_TextDim, Detail, Width - 12.0f);

			// a double click plays it, which is what the reference editor does
			if(bHover && Input.MousePressed(engine::CInput::mouse_button::left))
			{
				if(m_SelectedSound == i)
					PreviewSound(i);
				m_SelectedSound = i;
				m_SelectedImage = -1;
			}
			y += AssetRowHeight + 2.0f;
		}

		// the sound actions live at the bottom, where a long list of images
		// cannot push them off the panel
		const float ActionsY = Area.m_BottomRight.y - 56.0f;
		if(ActionsY > y + 4.0f)
		{
			const float ButtonWidth = std::max(50.0f, (Width - 8.0f) / 3.0f);
			float bx = X;
			const bool bEnabled = m_SelectedSound >= 0;
			const base::rect Play = base::rect::FromSize(base::vec2(bx, ActionsY), base::vec2(ButtonWidth, 22.0f));
			if(ui::Hovered(Input, Play))
				Hint(Play, Tr("Play") ,  bEnabled ? Tr("play the selected sound through the audio device") :
					Tr("select a sound to play it") );
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Play, Tr("Play") , false, bEnabled) && bEnabled)
				PreviewSound(m_SelectedSound);
			bx += ButtonWidth + 4.0f;
			const base::rect Stop = base::rect::FromSize(base::vec2(bx, ActionsY), base::vec2(ButtonWidth, 22.0f));
			if(ui::Hovered(Input, Stop))
				Hint(Stop, Tr("Stop") , Tr("stop the preview") );
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Stop, Tr("Stop") , false, SoundPreviewPlaying()))
				StopSoundPreview();
			bx += ButtonWidth + 4.0f;
			const base::rect Import = base::rect::FromSize(base::vec2(bx, ActionsY), base::vec2(ButtonWidth, 22.0f));
			if(ui::Hovered(Input, Import))
				Hint(Import, Tr("Import") , Tr("bring an .opus in as a sound of the map") );
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Import, Tr("Import") ))
				ImportSoundDialog();

			const std::string Now = SoundPreviewName();
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, ActionsY + 36.0f), 10.5f, ui::Theme.m_TextDim,
				Now.empty() ? "no sound playing" : "playing " + Now, Width);
		}
	}
} // namespace game

#endif
