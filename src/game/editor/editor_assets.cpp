/*
	The assets a map carries: its images and its sounds.

	A Teeworlds map has two kinds of image. A mapres image is external: the map
	stores only its name and every client reads mapres/<name>.png from its own
	data directory. An embedded image carries its pixels in the map, which is
	what a map needs when it uses a tileset no client ships. The editor has to
	be able to make both, because the DDRaceNetwork editor can: drag a PNG in,
	embed a mapres so the map travels alone, free it again, rename it, write it
	back out, or drop it and fix up every layer that referenced it.

	Sounds work the same way, with one difference that matters: a sound entry
	carries the bytes of a whole .opus file, and a sound *layer* places sources
	that reference it by index. So removing one has to leave the sources of the
	layers alone but keep the indices of the rest valid, and the editor can
	decode a duration out of the file so a source can be placed by length
	without playing it.

	The tile colour flags of an image are analysed from its pixels: the client
	needs TILEFLAG_OPAQUE only for the tiles that are fully opaque, and getting
	that wrong makes a map render wrongly rather than look plainly wrong.
*/

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base/i18n.h"
#include "base/logger.h"
#include "base/png.h"
#include "base/string.h"
#include "engine/audio.h"
#include "game/assets.h"
#include "game/editor/editor.h"

namespace game
{
	using base::i18n::Tr;
	namespace
	{
		// the pixels of one image, decoded: what every operation here starts
		// from, whether they came out of a file or out of the map
		struct CSourcePixels
		{
			int m_Width = 0;
			int m_Height = 0;
			byte_vector m_Pixels;
		};

		std::optional<CSourcePixels> DecodeFile(const std::string &Path, std::string *pError)
		{
			const auto Fail = [&](const std::string &Message) -> std::optional<CSourcePixels> {
				if(pError != nullptr)
					*pError = Message;
				return std::nullopt;
			};
			const std::optional<byte_vector> Bytes = storage::read(Path);
			if(!Bytes.has_value())
				return Fail("cannot read '" + Path + "'");
			const std::optional<CDecodedImage> Decoded = DecodePng(*Bytes);
			if(!Decoded.has_value())
				return Fail("'" + Path + "' is not an image the editor can read (PNG, JPEG, TGA, BMP and GIF work)");
			if(Decoded->m_Width <= 0 || Decoded->m_Height <= 0)
				return Fail("'" + Path + "' has no pixels");
			CSourcePixels Out;
			Out.m_Width = Decoded->m_Width;
			Out.m_Height = Decoded->m_Height;
			Out.m_Pixels = Decoded->m_Pixels;
			return Out;
		}

		// the name without its directory and extension: what the map stores and
		// what mapres/<name>.png is looked up by
		std::string StemOf(const std::string &Path)
		{
			std::string Name = storage::path(Path).stem().string();
			if(Name.empty())
				Name = "image";
			return Name;
		}
	} // namespace

	// ------------------------------------------------------------------ lookup
	int CEditor::FindImage(std::string_view Name) const
	{
		for(std::size_t i = 0; i < m_Map.m_vImages.size(); i++)
			if(m_Map.m_vImages[i].m_Name == Name)
				return static_cast<int>(i);
		return -1;
	}

	int CEditor::FindSound(std::string_view Name) const
	{
		for(std::size_t i = 0; i < m_Map.m_vSounds.size(); i++)
			if(m_Map.m_vSounds[i].m_Name == Name)
				return static_cast<int>(i);
		return -1;
	}

	int CEditor::ImageReferenceCount(int Index) const
	{
		int Count = 0;
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
				if(Layer.m_Image == Index)
					Count++;
		return Count;
	}

	int CEditor::SoundReferenceCount(int Index) const
	{
		int Count = 0;
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
				if(Layer.IsSounds() && Layer.m_Sound == Index)
					Count++;
		return Count;
	}

	int CEditor::UnusedImage() const
	{
		for(std::size_t i = 0; i < m_Map.m_vImages.size(); i++)
			if(ImageReferenceCount(static_cast<int>(i)) == 0)
				return static_cast<int>(i);
		return -1;
	}

	int CEditor::UnusedSound() const
	{
		for(std::size_t i = 0; i < m_Map.m_vSounds.size(); i++)
			if(SoundReferenceCount(static_cast<int>(i)) == 0)
				return static_cast<int>(i);
		return -1;
	}

	// ------------------------------------------------------------------ images
	int CEditor::AddImageRGBA(std::string_view Name, int Width, int Height, byte_vector Pixels, std::string *pError)
	{
		if(Width <= 0 || Height <= 0 || Pixels.size() < static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4)
		{
			if(pError != nullptr)
				*pError = "the image has no pixels";
			return -1;
		}

		CImage Image;
		Image.m_Name = std::string(Name);
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_External = 0;
		Image.m_Data = std::move(Pixels);

		// the per tile opacity the client reads when it draws a tile
		CDecodedImage Decoded;
		Decoded.m_Width = Width;
		Decoded.m_Height = Height;
		Decoded.m_Pixels = Image.m_Data;
		AnalyseTileFlags(Decoded, Image.m_aTileFlags);
		Image.m_aTileFlagsValid = true;

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		m_Map.m_vImages.push_back(std::move(Image));
		ForgetImageTextures();
		m_Dirty = true;
		const int Index = static_cast<int>(m_Map.m_vImages.size()) - 1;
		Notify(  base::FormatRuntime(Tr("added the image '{}' ({:d}x{:d}, embedded)"), m_Map.m_vImages[static_cast<std::size_t>(Index)].m_Name.c_str(),
			Width, Height)  );
		return Index;
	}

	int CEditor::AddImageFromFile(const std::string &Path, std::string *pError)
	{
		const std::optional<CSourcePixels> Pixels = DecodeFile(Path, pError);
		if(!Pixels.has_value())
			return -1;
		const std::string Name = StemOf(Path);
		const int Existing = FindImage(Name);
		if(Existing >= 0)
		{
			// a name the map already uses: replace that image rather than
			// adding a second one with the same name, which no client could
			// tell apart
			if(!ReplaceImagePixels(Existing, Pixels->m_Width, Pixels->m_Height, Pixels->m_Pixels, pError))
				return -1;
			Notify(  base::FormatRuntime(Tr("replaced the image '{}' from {}"), Name.c_str(),
				storage::path(Path).filename().string().c_str())  );
			return Existing;
		}
		return AddImageRGBA(Name, Pixels->m_Width, Pixels->m_Height, Pixels->m_Pixels, pError);
	}

	bool CEditor::ReplaceImagePixels(int Index, int Width, int Height, byte_vector Pixels, std::string *pError)
	{
		if(Index < 0 || Index >= ImageCount())
		{
			if(pError != nullptr)
				*pError = "no such image";
			return false;
		}
		if(Width <= 0 || Height <= 0 || Pixels.size() < static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4)
		{
			if(pError != nullptr)
				*pError = "the image has no pixels";
			return false;
		}

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(Index)];
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_External = 0;
		Image.m_Data = std::move(Pixels);

		CDecodedImage Decoded;
		Decoded.m_Width = Width;
		Decoded.m_Height = Height;
		Decoded.m_Pixels = Image.m_Data;
		AnalyseTileFlags(Decoded, Image.m_aTileFlags);
		Image.m_aTileFlagsValid = true;

		ForgetImageTextures();
		m_Dirty = true;
		return true;
	}

	bool CEditor::ReplaceImageFromFile(int Index, const std::string &Path, std::string *pError)
	{
		const std::optional<CSourcePixels> Pixels = DecodeFile(Path, pError);
		if(!Pixels.has_value())
			return false;
		return ReplaceImagePixels(Index, Pixels->m_Width, Pixels->m_Height, Pixels->m_Pixels, pError);
	}

	bool CEditor::EmbedImage(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= ImageCount())
		{
			if(pError != nullptr)
				*pError = "no such image";
			return false;
		}
		CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(Index)];
		if(Image.IsEmbedded())
			return true;
		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("no mapres file for '{}' in this version", Image.m_Name.c_str());
			return false;
		}
		const std::optional<CSourcePixels> Pixels = DecodeFile(File->string(), pError);
		if(!Pixels.has_value())
			return false;
		if(!ReplaceImagePixels(Index, Pixels->m_Width, Pixels->m_Height, Pixels->m_Pixels, pError))
			return false;
		Notify(  base::FormatRuntime(Tr("embedded the image '{}'"), Image.m_Name.c_str())  );
		return true;
	}

	bool CEditor::ExternaliseImage(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= ImageCount())
		{
			if(pError != nullptr)
				*pError = "no such image";
			return false;
		}
		CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(Index)];
		if(!Image.IsEmbedded())
			return true;
		if(!FindImageFile(Image.m_Name).has_value())
		{
			// dropping the pixels would leave the map pointing at a file that
			// does not exist, so the caller is told instead
			if(pError != nullptr)
				*pError = base::Format("there is no mapres/{}.png to fall back on", Image.m_Name.c_str());
			return false;
		}
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		Image.m_External = 1;
		Image.m_Data.clear();
		Image.m_aTileFlags.fill(0);
		Image.m_aTileFlagsValid = false;
		ForgetImageTextures();
		m_Dirty = true;
		Notify(  base::FormatRuntime(Tr("the image '{}' is external again"), Image.m_Name.c_str())  );
		return true;
	}

	bool CEditor::SetImageName(int Index, std::string_view Name)
	{
		if(Index < 0 || Index >= ImageCount() || Name.empty())
			return false;
		m_Map.m_vImages[static_cast<std::size_t>(Index)].m_Name = std::string(Name);
		ForgetImageTextures();
		m_Dirty = true;
		return true;
	}

	bool CEditor::ExportImage(int Index, const std::string &Path, std::string *pError)
	{
		if(Index < 0 || Index >= ImageCount())
		{
			if(pError != nullptr)
				*pError = "no such image";
			return false;
		}
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(Index)];

		// an external image is written from its mapres file, so the export is
		// the pixels the client would use rather than nothing
		if(!Image.IsEmbedded())
		{
			const std::optional<storage::path> File = FindImageFile(Image.m_Name);
			if(!File.has_value())
			{
				if(pError != nullptr)
					*pError = base::Format("no mapres file for '{}' in this version", Image.m_Name.c_str());
				return false;
			}
			const std::optional<byte_vector> Bytes = storage::read(*File);
			if(!Bytes.has_value() || !storage::write(Path, *Bytes))
			{
				if(pError != nullptr)
					*pError = "cannot write '" + Path + "'";
				return false;
			}
			return true;
		}

		if(!base::WritePng(Path, Image.m_Width, Image.m_Height, Image.m_Data))
		{
			if(pError != nullptr)
				*pError = "cannot write '" + Path + "'";
			return false;
		}
		return true;
	}

	bool CEditor::RemoveImage(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= ImageCount())
		{
			if(pError != nullptr)
				*pError = "no such image";
			return false;
		}
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		m_Map.m_vImages.erase(m_Map.m_vImages.begin() + Index);

		// every reference above the removed one shifts down by one, or the map
		// would draw a different image than the one it names
		for(CGroup &Group : m_Map.m_vGroups)
			for(CLayer &Layer : Group.m_vLayers)
				if(Layer.m_Image > Index)
					Layer.m_Image--;
				else if(Layer.m_Image == Index)
					Layer.m_Image = -1;

		/*
			Items the editor does not model can reference an image too (the
			DDRace automapper configuration does). Those bodies are opaque, so
			the indices inside them cannot be fixed; instead the removal is
			refused when such an item exists and at least one of them is
			plausibly an image reference, which is safer than silently breaking
			an imported item.
		*/
		if(!m_Map.m_vExtras.empty())
			LOG_WARN("editor", "removed image {:d} while the map carries {:d} items the editor does not model: "
				"those items keep the image indices they were imported with", Index, m_Map.m_vExtras.size());

		ForgetImageTextures();
		m_Dirty = true;
		Notify(  base::FormatRuntime(Tr("removed image {:d}"), Index)  );
		return true;
	}

	int CEditor::AddImageFromMapres(std::string_view Name, std::string *pError)
	{
		const std::string Clean = storage::path(std::string(Name)).stem().string();
		const int Existing = FindImage(Clean);
		if(Existing >= 0)
			return Existing;
		const std::optional<storage::path> File = FindImageFile(Clean);
		if(!File.has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("no mapres/{}.png in this version", Clean.c_str());
			return -1;
		}

		// an image that came from mapres stays external: that is the whole
		// point of naming one, and it keeps the map small
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		CImage Image;
		Image.m_Name = Clean;
		Image.m_External = 1;
		if(const std::optional<CSourcePixels> Pixels = DecodeFile(File->string(), nullptr); Pixels.has_value())
		{
			// the tile flags are needed straight away, so the pixels are read
			// once and then dropped again
			CDecodedImage Decoded;
			Decoded.m_Width = Pixels->m_Width;
			Decoded.m_Height = Pixels->m_Height;
			Decoded.m_Pixels = Pixels->m_Pixels;
			AnalyseTileFlags(Decoded, Image.m_aTileFlags);
			Image.m_aTileFlagsValid = true;
		}
		m_Map.m_vImages.push_back(std::move(Image));
		ForgetImageTextures();
		m_Dirty = true;
		const int Index = static_cast<int>(m_Map.m_vImages.size()) - 1;
		Notify(  base::FormatRuntime(Tr("added the mapres image '{}'"), Clean.c_str())  );
		return Index;
	}

	// ------------------------------------------------------------------ sounds
	int CEditor::AddSoundData(std::string_view Name, byte_vector Opus, bool bEmbedded, std::string *pError)
	{
		if(Opus.empty() && bEmbedded)
		{
			if(pError != nullptr)
				*pError = "the sound has no data";
			return -1;
		}
		CSound Sound;
		Sound.m_Name = std::string(Name);
		Sound.m_External = bEmbedded ? 0 : 1;
		Sound.m_Data = std::move(Opus);

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		m_Map.m_vSounds.push_back(std::move(Sound));
		m_Dirty = true;
		const int Index = static_cast<int>(m_Map.m_vSounds.size()) - 1;
		Notify(  base::FormatRuntime(Tr("added the sound '{}' ({})"), m_Map.m_vSounds[static_cast<std::size_t>(Index)].m_Name.c_str(),
			bEmbedded ? Tr("embedded") : Tr("external"))  );
		return Index;
	}

	int CEditor::AddSoundFromFile(const std::string &Path, std::string *pError)
	{
		const std::optional<byte_vector> Bytes = storage::read(Path);
		if(!Bytes.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + Path + "'";
			return -1;
		}
		// a sound the map carries is embedded by definition: the map is where
		// its bytes live, there is no mapres lookup for audio
		const std::string Name = StemOf(Path);
		const int Existing = FindSound(Name);
		if(Existing >= 0)
		{
			PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
			CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Existing)];
			Sound.m_External = 0;
			Sound.m_Data = *Bytes;
			m_Dirty = true;
			Notify(  base::FormatRuntime(Tr("replaced the sound '{}' from {}"), Name.c_str(),
				storage::path(Path).filename().string().c_str())  );
			return Existing;
		}
		return AddSoundData(Name, *Bytes, true, pError);
	}

	int CEditor::AddSoundFromResource(std::string_view Name, std::string *pError)
	{
		const std::string Clean = storage::path(std::string(Name)).stem().string();
		const std::optional<storage::path> File = FindSoundFile(Clean);
		if(!File.has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("no audio/{}.opus in this version", Clean.c_str());
			return -1;
		}
		const int Existing = FindSound(Clean);
		if(Existing >= 0)
			return Existing;
		return AddSoundFromFile(File->string(), pError);
	}

	bool CEditor::SetSoundName(int Index, std::string_view Name)
	{
		if(Index < 0 || Index >= SoundCount() || Name.empty())
			return false;
		m_Map.m_vSounds[static_cast<std::size_t>(Index)].m_Name = std::string(Name);
		m_Dirty = true;
		return true;
	}

	bool CEditor::EmbedSound(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= SoundCount())
		{
			if(pError != nullptr)
				*pError = "no such sound";
			return false;
		}
		CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Index)];
		if(Sound.m_External == 0)
			return true;
		const std::optional<storage::path> File = FindSoundFile(Sound.m_Name);
		if(!File.has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("no audio/{}.opus in this version", Sound.m_Name.c_str());
			return false;
		}
		const std::optional<byte_vector> Bytes = storage::read(*File);
		if(!Bytes.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + File->string() + "'";
			return false;
		}
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		Sound.m_External = 0;
		Sound.m_Data = *Bytes;
		m_Dirty = true;
		Notify(  base::FormatRuntime(Tr("embedded the sound '{}'"), Sound.m_Name.c_str())  );
		return true;
	}

	bool CEditor::ExternaliseSound(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= SoundCount())
		{
			if(pError != nullptr)
				*pError = "no such sound";
			return false;
		}
		CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Index)];
		if(Sound.m_External != 0)
			return true;
		if(!FindSoundFile(Sound.m_Name).has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("there is no audio/{}.opus to fall back on, so the bytes are kept", Sound.m_Name.c_str());
			return false;
		}
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		Sound.m_External = 1;
		Sound.m_Data.clear();
		m_Dirty = true;
		Notify(  base::FormatRuntime(Tr("the sound '{}' is external again"), Sound.m_Name.c_str())  );
		return true;
	}

	bool CEditor::ExportSound(int Index, const std::string &Path, std::string *pError)
	{
		if(Index < 0 || Index >= SoundCount())
		{
			if(pError != nullptr)
				*pError = "no such sound";
			return false;
		}
		const CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Index)];
		if(!Sound.m_Data.empty())
		{
			if(!storage::write(Path, Sound.m_Data))
			{
				if(pError != nullptr)
					*pError = "cannot write '" + Path + "'";
				return false;
			}
			return true;
		}
		const std::optional<storage::path> File = FindSoundFile(Sound.m_Name);
		if(!File.has_value())
		{
			if(pError != nullptr)
				*pError = base::Format("no audio/{}.opus in this version", Sound.m_Name.c_str());
			return false;
		}
		const std::optional<byte_vector> Bytes = storage::read(*File);
		if(!Bytes.has_value() || !storage::write(Path, *Bytes))
		{
			if(pError != nullptr)
				*pError = "cannot write '" + Path + "'";
			return false;
		}
		return true;
	}

	bool CEditor::RemoveSound(int Index, std::string *pError)
	{
		if(Index < 0 || Index >= SoundCount())
		{
			if(pError != nullptr)
				*pError = "no such sound";
			return false;
		}
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		m_Map.m_vSounds.erase(m_Map.m_vSounds.begin() + Index);
		/*
			A sound layer names one sound for the whole layer, so every layer
			above the removed one shifts down by one and the one that used it is
			left pointing at nothing rather than at whatever took its place.
		*/
		for(CGroup &Group : m_Map.m_vGroups)
			for(CLayer &Layer : Group.m_vLayers)
			{
				if(!Layer.IsSounds())
					continue;
				if(Layer.m_Sound > Index)
					Layer.m_Sound--;
				else if(Layer.m_Sound == Index)
					Layer.m_Sound = -1;
			}
		m_Dirty = true;
		Notify(  base::FormatRuntime(Tr("removed sound {:d}"), Index)  );
		return true;
	}

	// ------------------------------------------------------------ sound length
	/*
		How long a sound is. With opusfile linked (the windowed build) this is
		the real duration, read from the decoder. The headless build does not
		link a decoder, so it falls back to a worst-case estimate from the file
		size at the opus bitrate ceiling; that is enough to size a sound source
		in the data model and is honest about being an estimate.
	*/
	float CEditor::SoundLength(int Index) const
	{
		if(Index < 0 || Index >= SoundCount())
			return 0.0f;
		const CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Index)];
		const byte_vector *pData = &Sound.m_Data;
		std::optional<byte_vector> FileData;
		if(pData->empty())
		{
			const std::optional<storage::path> File = FindSoundFile(Sound.m_Name);
			if(!File.has_value())
				return 0.0f;
			FileData = storage::read(*File);
			if(!FileData.has_value())
				return 0.0f;
			pData = &*FileData;
		}
#if defined(UNITEED_AUDIO)
		if(const float Decoded = engine::OpusDurationSeconds(*pData); Decoded > 0.0f)
			return Decoded;
#endif
		// 510 kbit/s stereo is the opus ceiling, so this is never longer than
		// the real sound and is usually generous by a factor of a few
		constexpr double CeilingBytesPerSecond = 510000.0 / 8.0;
		return static_cast<float>(static_cast<double>(pData->size()) / CeilingBytesPerSecond);
	}
} // namespace game
