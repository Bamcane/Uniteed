#ifndef GAME_ASSETS_H
#define GAME_ASSETS_H

/*
	Version aware asset lookup.

	0.6 and 0.7 ship the same mapres file names with different pixels (every one
	of the 28 shared names differs, most 0.7 backdrops are twice the resolution),
	so the images live in per version directories:

		data/shared/mapres/<file>          identical in every version
		data/versions/06/mapres/<file>     vanilla Teeworlds 0.6
		data/versions/07/mapres/<file>     vanilla Teeworlds 0.7 and 0.8 (same bytes)
		data/versions/ddnet/mapres/<file>  DDRaceNetwork, including the _0.7 twins

	The resource paths are ordered so that the directory of the active version
	wins, DDRace extras are still found, and the other version stays reachable
	as a fallback. See tools/sync_assets.sh for how the directories are filled.
*/

#include <optional>
#include <string>
#include <vector>

#include "base/storage.h"
#include "game/map/map.h"

namespace game
{
	// the asset directory of a target ("06", "07" or "ddnet")
	std::string VersionIdForTarget(map_target Target);

	// installs the search order for a version
	void SetupResourcePaths(std::string_view VersionId);

	// "grass_main" -> the path of mapres/grass_main.png, when it exists
	std::optional<storage::path> FindImageFile(std::string_view Name);
	std::optional<storage::path> FindSoundFile(std::string_view Name);

	// the directories that exist, for diagnostics
	std::vector<storage::path> ExistingVersionDirectories();

	// decodes a PNG file into raw RGBA, used to analyse the tile flags of
	// embedded images and to build textures
	struct CDecodedImage
	{
		int m_Width = 0;
		int m_Height = 0;
		byte_vector m_Pixels; // RGBA
	};
	std::optional<CDecodedImage> DecodePng(const byte_vector &Png);

	// the per tile opacity flags of an image, used when a tile is painted: the
	// client needs TILEFLAG_OPAQUE for the tiles that are fully opaque
	void AnalyseTileFlags(const CDecodedImage &Image, std::array<std::uint8_t, 256> &Out);
} // namespace game

#endif // GAME_ASSETS_H
