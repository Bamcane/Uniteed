#include "assets.h"

#include <algorithm>
#include <cstring>

#include "base/logger.h"

// the decoder of image_codec.cpp, which keeps stb_image to itself so that the
// copy inside nanovg does not clash with it
extern "C" unsigned char *uniteed_stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp);
extern "C" void uniteed_stbi_image_free(void *data);

namespace game
{
	namespace
	{
		std::string s_ActiveVersion = "07";
	} // namespace

	std::string VersionIdForTarget(map_target Target)
	{
		switch(Target)
		{
		case map_target::tee06: return "06";
		case map_target::tee07: return "07";
		case map_target::tee08: return "07"; // 0.8 ships the same bytes as 0.7
		case map_target::ddnet: return "ddnet";
		case map_target::unitwp: return "07";
		}
		return "07";
	}

	std::vector<storage::path> ExistingVersionDirectories()
	{
		std::vector<storage::path> Directories;
		const storage::path Base = storage::GetDataDirectory() / "versions";
		for(const char *pName : {"06", "07", "ddnet"})
		{
			const storage::path Directory = Base / pName;
			if(storage::exists(Directory))
				Directories.push_back(Directory);
		}
		return Directories;
	}

	void SetupResourcePaths(std::string_view VersionId)
	{
		s_ActiveVersion = std::string(VersionId);
		const storage::path Base = storage::GetDataDirectory();

		std::vector<storage::path> Paths;
		const auto Add = [&Paths](const storage::path &Directory) {
			if(!storage::exists(Directory))
				return;
			if(std::find(Paths.begin(), Paths.end(), Directory) != Paths.end())
				return;
			Paths.push_back(Directory);
		};

		// the active version wins, then the DDRace extras (maps often reference
		// ddnet_tiles.png and friends from a 0.6 or 0.7 map), then shared assets,
		// then the other vanilla version as a last resort
		Add(Base / "versions" / s_ActiveVersion / "mapres");
		Add(Base / "versions" / "ddnet" / "mapres");
		Add(Base / "shared" / "mapres");
		Add(Base / "versions" / "07" / "mapres");
		Add(Base / "versions" / "06" / "mapres");
		Add(Base / "mapres");

		storage::SetResourcePaths(std::move(Paths));
		LOG_TRACE("assets", "resource paths for version '%s': %zu directories", s_ActiveVersion.c_str(), storage::ResourcePaths().size());
	}

	std::optional<storage::path> FindImageFile(std::string_view Name)
	{
		std::string File(Name);
		if(File.empty())
			return std::nullopt;
		if(!base::EndsWith(File, ".png"))
			File += ".png";
		const storage::path Found = storage::FindResource(storage::path(File));
		if(Found.empty())
			return std::nullopt;
		return Found;
	}

	std::optional<storage::path> FindSoundFile(std::string_view Name)
	{
		std::string File(Name);
		if(File.empty())
			return std::nullopt;
		if(!base::EndsWith(File, ".opus"))
			File += ".opus";
		const storage::path Found = storage::FindResource(storage::path(File));
		if(Found.empty())
			return std::nullopt;
		return Found;
	}

	std::optional<CDecodedImage> DecodePng(const byte_vector &Png)
	{
		if(Png.empty())
			return std::nullopt;
		int Width = 0, Height = 0, Channels = 0;
		unsigned char *pPixels = uniteed_stbi_load_from_memory(
			reinterpret_cast<const unsigned char *>(Png.data()), static_cast<int>(Png.size()), &Width, &Height, &Channels, 4);
		if(pPixels == nullptr)
			return std::nullopt;

		CDecodedImage Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Pixels.assign(reinterpret_cast<std::byte *>(pPixels), reinterpret_cast<std::byte *>(pPixels) + static_cast<std::size_t>(Width) * Height * 4);
		uniteed_stbi_image_free(pPixels);
		return Image;
	}

	void AnalyseTileFlags(const CDecodedImage &Image, std::array<std::uint8_t, 256> &Out)
	{
		Out.fill(0);
		if(Image.m_Width <= 0 || Image.m_Height <= 0 || Image.m_Pixels.size() < static_cast<std::size_t>(Image.m_Width) * Image.m_Height * 4)
			return;

		// a Teeworlds tile sheet is a 16 by 16 grid of tiles
		const int TileWidth = std::max(1, Image.m_Width / 16);
		const int TileHeight = std::max(1, Image.m_Height / 16);

		for(int Index = 0; Index < 256; Index++)
		{
			const int BaseX = (Index % 16) * TileWidth;
			const int BaseY = (Index / 16) * TileHeight;
			bool Opaque = true;
			for(int y = 0; y < TileHeight && Opaque; y++)
			{
				const int PixelY = BaseY + y;
				if(PixelY >= Image.m_Height)
					break;
				for(int x = 0; x < TileWidth; x++)
				{
					const int PixelX = BaseX + x;
					if(PixelX >= Image.m_Width)
						break;
					const std::size_t Offset = (static_cast<std::size_t>(PixelY) * Image.m_Width + PixelX) * 4 + 3;
					if(Image.m_Pixels[Offset] != std::byte(255))
					{
						Opaque = false;
						break;
					}
				}
			}
			if(Opaque)
				Out[static_cast<std::size_t>(Index)] = TILEFLAG_OPAQUE;
		}
	}
} // namespace game
