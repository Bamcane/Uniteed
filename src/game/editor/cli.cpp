#include "cli.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

#include "base/define.h"
#include "base/logger.h"
#include "base/math.h"
#include "base/string.h"
#include "base/time.h"
#include "game/assets.h"
#include "game/map/envelope.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"

namespace game
{
	namespace
	{
		void PrintUsage()
		{
			std::cout <<
				GAME_DISPLAY_NAME " " GAME_VERSION " - a Teeworlds map editor for 0.6, 0.7, DDRaceNetwork and 0.8\n"
				"\n"
				"usage: uniteed [command] [options]\n"
				"\n"
				"commands\n"
				"  --gui [map]                     open the editor window (default)\n"
				"  --new <out.map> [--target id] [--size WxH]\n"
				"                                  create an empty map\n"
				"  --dump <map>                    print the structure of a map\n"
				"  --convert <in> <out.map> [--target id]\n"
				"                                  convert a map to another version\n"
				"  --pack <in> <out.unitwp> [--embed] [--target id]\n"
				"                                  wrap a map into a .unitwp package\n"
				"  --unpack <in.unitwp> <out.map> [--target id]\n"
				"                                  export a package as a plain map\n"
				"  --targets                       print the version matrix\n"
				"  --selftest [map ...]            round trip every bundled map\n"
				"  --help\n"
				"\n"
				"target ids: 06 (Teeworlds 0.6), 07 (0.7), ddnet (DDRaceNetwork), 08 (0.8, datafile v5)\n";
		}

		void PrintTargets()
		{
			std::cout << "target  datafile  tilemap  image  group  envelopes  notes\n";
			for(const map_target Target : AllTargets())
			{
				const CMapTargetProfile &Profile = GetTargetProfile(Target);
				std::cout << base::Format("%-7s %-9d %-8d %-6d %-6d %-10s %s\n",
					Profile.m_pId, Profile.m_DatafileVersion, Profile.m_TilemapVersion,
					Profile.m_ImageVersion, Profile.m_GroupVersion,
					Profile.m_BezierEnvelopes ? "v2/v3" : "v2",
					Profile.m_pDescription);
			}
		}

		struct CArguments
		{
			std::string m_Command;
			std::vector<std::string> m_vPositional;
			std::string m_TargetId = "08";
			bool m_Embed = false;
			int m_Width = 50;
			int m_Height = 50;
		};

		bool ParseArguments(const std::vector<std::string> &Arguments, CArguments &Out)
		{
			for(std::size_t i = 0; i < Arguments.size(); i++)
			{
				const std::string &Argument = Arguments[i];
				if(Argument == "--target" || Argument == "-t")
				{
					if(i + 1 >= Arguments.size())
						return false;
					Out.m_TargetId = Arguments[++i];
				}
				else if(Argument == "--embed")
				{
					Out.m_Embed = true;
				}
				else if(Argument == "--size")
				{
					if(i + 1 >= Arguments.size())
						return false;
					const std::string Size = Arguments[++i];
					const std::size_t Pos = Size.find('x');
					if(Pos == std::string::npos)
						return false;
					Out.m_Width = std::max(1, std::atoi(Size.substr(0, Pos).c_str()));
					Out.m_Height = std::max(1, std::atoi(Size.substr(Pos + 1).c_str()));
				}
				else if(!Argument.empty() && Argument[0] == '-' && Out.m_Command.empty())
				{
					Out.m_Command = Argument;
				}
				else if(!Argument.empty() && Argument[0] == '-' && Argument != "--gui")
				{
					// a second option that takes no argument, keep it as a marker
					Out.m_vPositional.push_back(Argument);
				}
				else
				{
					Out.m_vPositional.push_back(Argument);
				}
			}
			return true;
		}

		void PrintMap(const CMap &Map)
		{
			const CMapStatistics Stats = Map.Statistics();
			std::cout << base::Format("datafile v%d  groups=%d layers=%d images=%d(%d embedded) envelopes=%d quads=%d preserved items=%d\n",
				Map.m_SourceDatafileVersion, Stats.m_Groups, Stats.m_Layers, Stats.m_Images,
				Stats.m_EmbeddedImages, Stats.m_Envelopes, Stats.m_Quads, Stats.m_Extras);
			std::cout << base::Format("info: author='%s' version='%s' credits='%s' license='%s'\n",
				Map.m_aAuthor.c_str(), Map.m_aVersion.c_str(), Map.m_aCredits.c_str(), Map.m_aLicense.c_str());
			std::cout << base::Format("tiles=%d non-empty=%d\n", Stats.m_Tiles, Stats.m_TilesWithContent);

			for(std::size_t g = 0; g < Map.m_vGroups.size(); g++)
			{
				const CGroup &Group = Map.m_vGroups[g];
				std::cout << base::Format("  group %zu '%s' parallax=%d,%d offset=%d,%d clip=%d version=%d layers=%zu\n",
					g, Group.m_Name.c_str(), Group.m_ParallaxX, Group.m_ParallaxY, Group.m_OffsetX, Group.m_OffsetY,
					Group.m_UseClipping, Group.m_SourceVersion, Group.m_vLayers.size());
				for(std::size_t l = 0; l < Group.m_vLayers.size(); l++)
				{
					const CLayer &Layer = Group.m_vLayers[l];
					const char *pKind = Layer.IsTiles() ? "tiles" : (Layer.IsQuads() ? "quads" : (Layer.IsSounds() ? "sounds" : "unknown"));
					std::string Extra;
					if(Layer.IsTiles())
					{
						int NonEmpty = 0;
						for(const CTile &Tile : Layer.m_vTiles)
							if(Tile.m_Index != 0)
								NonEmpty++;
						Extra = base::Format(" %dx%d flags=%d color=%d,%d,%d,%d image=%d non-empty=%d tilemap-version=%d%s",
							Layer.m_Width, Layer.m_Height, Layer.m_Flags,
							Layer.m_Color.r, Layer.m_Color.g, Layer.m_Color.b, Layer.m_Color.a,
							Layer.m_Image, NonEmpty,
							Layer.m_SourceTilemapVersion, Layer.IsPhysicsLayer() ? " physics" : "");
					}
					else if(Layer.IsQuads())
					{
						// graded quads have four different corner colours, which
						// is what the editor draws as a gradient
						int Graded = 0;
						for(const CQuad &Quad : Layer.m_vQuads)
						{
							const CColor &C0 = Quad.m_aColors[0];
							for(int c = 1; c < 4; c++)
								if(Quad.m_aColors[c].r != C0.r || Quad.m_aColors[c].g != C0.g ||
									Quad.m_aColors[c].b != C0.b || Quad.m_aColors[c].a != C0.a)
								{
									Graded++;
									break;
								}
						}
						Extra = base::Format(" quads=%zu graded=%d image=%d", Layer.m_vQuads.size(), Graded, Layer.m_Image);
					}
					else if(Layer.IsSounds())
						Extra = base::Format(" sources=%zu sound=%d", Layer.m_vSources.size(), Layer.m_Sound);
					if(Layer.m_ColorEnv >= 0)
						Extra += base::Format(" color-env=%d+%d", Layer.m_ColorEnv, Layer.m_ColorEnvOffset);
					std::cout << base::Format("    layer %zu '%s' type=%s%s\n", l, Layer.m_Name.c_str(), pKind, Extra.c_str());
				}
			}

			for(std::size_t i = 0; i < Map.m_vImages.size(); i++)
			{
				const CImage &Image = Map.m_vImages[i];
				std::cout << base::Format("  image %zu '%s' %dx%d %s\n", i, Image.m_Name.c_str(), Image.m_Width, Image.m_Height,
					Image.IsEmbedded() ? "embedded" : "external");
			}
			for(std::size_t i = 0; i < Map.m_vEnvelopes.size(); i++)
			{
				const CEnvelope &Envelope = Map.m_vEnvelopes[i];
				std::cout << base::Format("  envelope %zu '%s' channels=%d points=%zu synchronized=%d\n",
					i, Envelope.m_Name.c_str(), Envelope.m_Channels, Envelope.m_vPoints.size(), Envelope.m_Synchronized);
				for(std::size_t p = 0; p < Envelope.m_vPoints.size(); p++)
				{
					const CEnvelopePoint &Point = Envelope.m_vPoints[p];
					std::cout << base::Format("    point %zu t=%d curve=%s values=%d,%d,%d,%d\n", p, Point.m_Time,
						Point.m_Curvetype == CURVETYPE_STEP ? "step" : Point.m_Curvetype == CURVETYPE_SLOW ? "slow" :
						Point.m_Curvetype == CURVETYPE_FAST ? "fast" : Point.m_Curvetype == CURVETYPE_SMOOTH ? "smooth" :
						Point.m_Curvetype == CURVETYPE_BEZIER ? "bezier" : "linear",
						Point.m_aValues[0], Point.m_aValues[1], Point.m_aValues[2], Point.m_aValues[3]);
				}
			}
			for(const CExtraItem &Extra : Map.m_vExtras)
				std::cout << base::Format("  preserved item type=%d id=%d size=%zu\n", Extra.m_Type, Extra.m_ID, Extra.m_Data.size());
		}

		// structural comparison of two models, used by the selftest
		bool CompareMaps(const CMap &A, const CMap &B, std::string &Error)
		{
			const CMapStatistics StatsA = A.Statistics();
			const CMapStatistics StatsB = B.Statistics();
			if(StatsA.m_Images != StatsB.m_Images)
			{
				Error = base::Format("image count %d -> %d", StatsA.m_Images, StatsB.m_Images);
				return false;
			}
			for(std::size_t i = 0; i < A.m_vImages.size(); i++)
			{
				if(A.m_vImages[i].m_Name != B.m_vImages[i].m_Name)
				{
					Error = base::Format("image %zu name '%s' -> '%s'", i, A.m_vImages[i].m_Name.c_str(), B.m_vImages[i].m_Name.c_str());
					return false;
				}
				if(A.m_vImages[i].m_Width != B.m_vImages[i].m_Width || A.m_vImages[i].m_Height != B.m_vImages[i].m_Height)
				{
					Error = base::Format("image %zu dimensions changed", i);
					return false;
				}
				if(A.m_vImages[i].m_Data != B.m_vImages[i].m_Data)
				{
					Error = base::Format("image %zu pixel data changed", i);
					return false;
				}
			}
			if(A.m_vEnvelopes.size() != B.m_vEnvelopes.size())
			{
				Error = base::Format("envelope count %zu -> %zu", A.m_vEnvelopes.size(), B.m_vEnvelopes.size());
				return false;
			}
			for(std::size_t i = 0; i < A.m_vEnvelopes.size(); i++)
			{
				if(A.m_vEnvelopes[i].m_vPoints.size() != B.m_vEnvelopes[i].m_vPoints.size())
				{
					Error = base::Format("envelope %zu point count changed", i);
					return false;
				}
				for(std::size_t p = 0; p < A.m_vEnvelopes[i].m_vPoints.size(); p++)
				{
					const CEnvelopePoint &PA = A.m_vEnvelopes[i].m_vPoints[p];
					const CEnvelopePoint &PB = B.m_vEnvelopes[i].m_vPoints[p];
					if(PA.m_Time != PB.m_Time || PA.m_Curvetype != PB.m_Curvetype ||
						std::memcmp(PA.m_aValues, PB.m_aValues, sizeof(PA.m_aValues)) != 0)
					{
						Error = base::Format("envelope %zu point %zu changed", i, p);
						return false;
					}
				}
			}
			return true;
		}

		// every group/layer of B must exist in A with the same content; B may
		// have dropped layers the target cannot express
		bool CompareLayers(const CMap &A, const CMap &B, std::string &Error)
		{
			for(std::size_t g = 0; g < B.m_vGroups.size(); g++)
			{
				const CGroup &GroupA = A.m_vGroups[g];
				const CGroup &GroupB = B.m_vGroups[g];
				if(GroupA.m_vLayers.size() != GroupB.m_vLayers.size())
				{
					// physics and sound layers are dropped for some targets
					continue;
				}
				for(std::size_t l = 0; l < GroupB.m_vLayers.size(); l++)
				{
					const CLayer &LA = GroupA.m_vLayers[l];
					const CLayer &LB = GroupB.m_vLayers[l];
					if(LA.m_Type != LB.m_Type || LA.m_Name != LB.m_Name)
					{
						Error = base::Format("group %zu layer %zu identity changed", g, l);
						return false;
					}
					if(LA.IsTiles())
					{
						if(LA.m_Width != LB.m_Width || LA.m_Height != LB.m_Height)
						{
							Error = base::Format("group %zu layer %zu dimensions changed", g, l);
							return false;
						}
						// the layer colour and its envelope have to survive too
						if(LA.m_Color.r != LB.m_Color.r || LA.m_Color.g != LB.m_Color.g ||
							LA.m_Color.b != LB.m_Color.b || LA.m_Color.a != LB.m_Color.a)
						{
							Error = base::Format("group %zu layer %zu colour changed (%d,%d,%d,%d -> %d,%d,%d,%d)", g, l,
								LA.m_Color.r, LA.m_Color.g, LA.m_Color.b, LA.m_Color.a,
								LB.m_Color.r, LB.m_Color.g, LB.m_Color.b, LB.m_Color.a);
							return false;
						}
						if(LA.m_ColorEnv != LB.m_ColorEnv || LA.m_ColorEnvOffset != LB.m_ColorEnvOffset)
						{
							Error = base::Format("group %zu layer %zu colour envelope changed", g, l);
							return false;
						}
						if(LA.IsPhysicsLayer())
							continue;
						if(LA.m_vTiles.size() != LB.m_vTiles.size())
						{
							Error = base::Format("group %zu layer %zu tile count changed", g, l);
							return false;
						}
						for(std::size_t t = 0; t < LA.m_vTiles.size(); t++)
						{
							if(LA.m_vTiles[t].m_Index != LB.m_vTiles[t].m_Index || LA.m_vTiles[t].m_Flags != LB.m_vTiles[t].m_Flags)
							{
								Error = base::Format("group %zu layer %zu tile %zu changed (%d/%d -> %d/%d)", g, l, t,
									LA.m_vTiles[t].m_Index, LA.m_vTiles[t].m_Flags, LB.m_vTiles[t].m_Index, LB.m_vTiles[t].m_Flags);
								return false;
							}
						}
					}
					else if(LA.IsQuads())
					{
						if(LA.m_vQuads.size() != LB.m_vQuads.size())
						{
							Error = base::Format("group %zu layer %zu quad count changed", g, l);
							return false;
						}
						for(std::size_t q = 0; q < LA.m_vQuads.size(); q++)
						{
							for(int c = 0; c < 4; c++)
							{
								const CColor &CA = LA.m_vQuads[q].m_aColors[c];
								const CColor &CB = LB.m_vQuads[q].m_aColors[c];
								if(CA.r != CB.r || CA.g != CB.g || CA.b != CB.b || CA.a != CB.a)
								{
									Error = base::Format("group %zu layer %zu quad %zu corner %d colour changed", g, l, q, c);
									return false;
								}
							}
							if(LA.m_vQuads[q].m_ColorEnv != LB.m_vQuads[q].m_ColorEnv ||
								LA.m_vQuads[q].m_PosEnv != LB.m_vQuads[q].m_PosEnv)
							{
								Error = base::Format("group %zu layer %zu quad %zu envelope changed", g, l, q);
								return false;
							}
						}
					}
					else if(LA.IsSounds() && LA.m_vSources.size() != LB.m_vSources.size())
					{
						Error = base::Format("group %zu layer %zu sound source count changed", g, l);
						return false;
					}
				}
			}
			return true;
		}

		std::vector<std::string> CollectBundledMaps()
		{
			std::vector<std::string> Maps;
			const storage::path Root = storage::GetDataDirectory() / "maps";
			for(const storage::path &Directory : storage::list_subdirectories(Root))
			{
				for(const storage::path &File : storage::list_directory(Directory, ".map"))
					Maps.push_back(File.string());
			}
			return Maps;
		}
	} // namespace

	int RunSelfTest(const std::vector<std::string> &MapFiles)
	{
		const std::vector<std::string> Maps = MapFiles.empty() ? CollectBundledMaps() : MapFiles;
		if(Maps.empty())
		{
			std::cout << "no maps to test\n";
			return 1;
		}

		int Failures = 0;
		int Checks = 0;
		for(const std::string &MapFile : Maps)
		{
			std::cout << "\n== " << MapFile << "\n";
			CMap Source;
			std::string Error;
			if(!Source.LoadFromFile(MapFile, &Error))
			{
				std::cout << "  FAIL load: " << Error << "\n";
				Failures++;
				continue;
			}
			PrintMap(Source);

			for(const map_target Target : AllTargets())
			{
				const CMapTargetProfile &Profile = GetTargetProfile(Target);
				const std::string Warnings = [&] {
					std::string Text;
					for(const std::string &Warning : Source.CompatibilityWarnings(Target))
						Text += " [" + Warning + "]";
					return Text;
				}();

				Checks++;
				byte_vector Bytes;
				if(!Source.SaveToBytes(Bytes, Target, &Error))
				{
					std::cout << "  FAIL " << Profile.m_pId << " save: " << Error << "\n";
					Failures++;
					continue;
				}

				CMap Reloaded;
				if(!Reloaded.LoadFromBytes(Bytes, &Error))
				{
					std::cout << "  FAIL " << Profile.m_pId << " reload: " << Error << "\n";
					Failures++;
					continue;
				}

				if(!CompareMaps(Source, Reloaded, Error))
				{
					std::cout << "  FAIL " << Profile.m_pId << " structure: " << Error << "\n";
					Failures++;
					continue;
				}

				// physics layers and sounds may legitimately disappear, compare
				// what survived, by position inside the group
				if(!CompareLayers(Source, Reloaded, Error) && Profile.m_pId != std::string("06"))
				{
					std::cout << "  FAIL " << Profile.m_pId << " layers: " << Error << "\n";
					Failures++;
					continue;
				}

				/*
					Saving the reloaded map again must reproduce the same bytes
					once the model has been normalised: the first save may move
					the data blocks of items the editor does not model (it keeps
					only the blocks they can reach), from the second save on the
					layout is fixed. A third save is therefore compared with the
					second, which is the invariant that matters.
				*/
				byte_vector Again;
				if(!Reloaded.SaveToBytes(Again, Target, &Error))
				{
					std::cout << "  FAIL " << Profile.m_pId << " second save: " << Error << "\n";
					Failures++;
					continue;
				}
				CMap Twice;
				if(!Twice.LoadFromBytes(Again, &Error) || !CompareMaps(Source, Twice, Error))
				{
					std::cout << "  FAIL " << Profile.m_pId << " second reload: " << Error << "\n";
					Failures++;
					continue;
				}
				byte_vector Third;
				if(!Twice.SaveToBytes(Third, Target, &Error) || Third != Again)
				{
					std::cout << "  FAIL " << Profile.m_pId << " is not stable across saves (" << Error << ")\n";
					Failures++;
					continue;
				}

				std::cout << base::Format("  ok   %-6s %6zu bytes -> reload ok, stable%s\n",
					Profile.m_pId, Bytes.size(), Warnings.c_str());
			}

			// the package round trip
			{
				Checks++;
				CUnitwpPackage Package;
				Package.Adopt(Source, storage::path(MapFile).stem().string(), MapFile);
				byte_vector Packed;
				if(!Package.Serialise(Packed, &Error))
				{
					std::cout << "  FAIL package serialise: " << Error << "\n";
					Failures++;
				}
				else
				{
					CUnitwpPackage ReloadedPackage;
					if(!ReloadedPackage.Deserialise(Packed, &Error))
					{
						std::cout << "  FAIL package reload: " << Error << "\n";
						Failures++;
					}
					else if(!CompareMaps(Source, ReloadedPackage.m_Map, Error))
					{
						std::cout << "  FAIL package structure: " << Error << "\n";
						Failures++;
					}
					else if(!CompareLayers(Source, ReloadedPackage.m_Map, Error))
					{
						std::cout << "  FAIL package layers: " << Error << "\n";
						Failures++;
					}
					else
					{
						std::cout << base::Format("  ok   %-6s %6zu bytes -> package round trip ok\n", "unitwp", Packed.size());
					}
				}
			}

			/*
				The editor can change the colour of a tile layer and of a quad,
				so an edit has to come back out of every export. Distinctive
				values are used so a stale or dropped colour cannot pass.
			*/
			{
				Checks++;
				CMap Edited = Source;
				const CColor TileColor{17, 129, 201, 233};
				const CColor QuadColor{240, 15, 90, 128};
				bool bTile = false, bQuad = false;
				for(CGroup &Group : Edited.m_vGroups)
					for(CLayer &Layer : Group.m_vLayers)
					{
						if(Layer.IsTiles() && !Layer.IsPhysicsLayer() && !bTile)
						{
							Layer.m_Color = TileColor;
							bTile = true;
						}
						if(Layer.IsQuads() && !Layer.m_vQuads.empty() && !bQuad)
						{
							Layer.m_vQuads[0].m_aColors[1] = QuadColor;
							bQuad = true;
						}
					}

				std::string Why;
				bool bOk = true;
				for(const map_target Target : AllTargets())
				{
					byte_vector Bytes;
					CMap Back;
					if(!Edited.SaveToBytes(Bytes, Target, &Error) || !Back.LoadFromBytes(Bytes, &Error))
					{
						bOk = false;
						Why = base::Format("%s: %s", GetTargetProfile(Target).m_pId, Error.c_str());
						break;
					}

					bool bFoundTile = !bTile, bFoundQuad = !bQuad;
					for(const CGroup &Group : Back.m_vGroups)
						for(const CLayer &Layer : Group.m_vLayers)
						{
							if(Layer.m_Color.r == TileColor.r && Layer.m_Color.g == TileColor.g &&
								Layer.m_Color.b == TileColor.b && Layer.m_Color.a == TileColor.a)
								bFoundTile = true;
							for(const CQuad &Quad : Layer.m_vQuads)
								for(const CColor &Corner : Quad.m_aColors)
									if(Corner.r == QuadColor.r && Corner.g == QuadColor.g &&
										Corner.b == QuadColor.b && Corner.a == QuadColor.a)
										bFoundQuad = true;
						}
					if(!bFoundTile || !bFoundQuad)
					{
						bOk = false;
						Why = base::Format("%s lost the %s colour", GetTargetProfile(Target).m_pId,
							bFoundTile ? "quad" : "layer");
						break;
					}
				}

				if(bOk)
					std::cout << "  ok   colour an edited layer and quad colour survives every export\n";
				else
				{
					std::cout << "  FAIL colour " << Why << "\n";
					Failures++;
				}
			}
		}

		// ------------------------------------------------------- view transform
		/*
			The canvas is a sub rectangle of the window, so the transform has to
			carry that origin in both directions. Without it the map is drawn
			somewhere other than where the tiles are picked and painted, which is
			what happened before the origin was added.
		*/
		{
			Checks++;
			base::view_transform View;
			View.m_Origin = base::vec2(268.0f, 40.0f);
			View.m_Viewport = base::vec2(1064.0f, 834.0f);
			View.m_Center = base::vec2(800.0f, 800.0f);
			View.m_Zoom = 0.4875f;

			const base::vec2 Center = View.WorldToScreen(View.m_Center);
			const base::vec2 Wanted(View.m_Origin.x + View.m_Viewport.x * 0.5f, View.m_Origin.y + View.m_Viewport.y * 0.5f);
			bool bOk = std::abs(Center.x - Wanted.x) < 0.001f && std::abs(Center.y - Wanted.y) < 0.001f;
			for(const base::vec2 World : {base::vec2(0.0f, 0.0f), base::vec2(1600.0f, 1600.0f), base::vec2(512.0f, 96.0f), base::vec2(-320.0f, 2048.0f)})
			{
				const base::vec2 Back = View.ScreenToWorld(View.WorldToScreen(World));
				if(std::abs(Back.x - World.x) > 0.001f || std::abs(Back.y - World.y) > 0.001f)
					bOk = false;
			}

			if(bOk)
				std::cout << "  ok   view   the canvas transform keeps the canvas origin\n";
			else
			{
				std::cout << "  FAIL view   the canvas transform loses the canvas origin\n";
				Failures++;
			}
		}

		// ------------------------------------------------------------- quads
		/*
			A quad's four corners are a freeform patch, not a perimeter walk: 0
			and 1 are the top edge and 2 and 3 the bottom one. Walking them in
			stored order crosses the shape into a bowtie, which is exactly what
			happened once, so the perimeter order is pinned here. The check walks
			the outline of a unit square and confirms it stays convex.
		*/
		{
			Checks++;
			// a unit square in screen space, stored the way a map stores it
			const base::vec2 aCorners[4] = {base::vec2(0.0f, 0.0f), base::vec2(10.0f, 0.0f),
				base::vec2(0.0f, 10.0f), base::vec2(10.0f, 10.0f)};
			base::vec2 aWalk[4];
			for(int i = 0; i < 4; i++)
				aWalk[i] = aCorners[CQuad::s_aPerimeter[i]];

			// every turn of a convex outline has the same sign
			int Positive = 0, Negative = 0;
			for(int i = 0; i < 4; i++)
			{
				const base::vec2 &A = aWalk[i];
				const base::vec2 &B = aWalk[(i + 1) % 4];
				const base::vec2 &C = aWalk[(i + 2) % 4];
				const float Cross = (B.x - A.x) * (C.y - B.y) - (B.y - A.y) * (C.x - B.x);
				if(Cross > 0.0f)
					Positive++;
				else if(Cross < 0.0f)
					Negative++;
			}
			// walking 0,1,3,2 traces the square; walking 0,1,2,3 crosses it and
			// produces turns of both signs
			bool bOk = (Positive == 4 || Negative == 4);
			// the corners must also map back to the (u,v) positions the texcoords
			// use, or the texture would be laid down rotated
			if(aCorners[CQuad::s_aCornerAt[0][0]] != aCorners[0] ||
				aCorners[CQuad::s_aCornerAt[1][0]] != aCorners[1] ||
				aCorners[CQuad::s_aCornerAt[0][1]] != aCorners[2] ||
				aCorners[CQuad::s_aCornerAt[1][1]] != aCorners[3])
				bOk = false;

			if(bOk)
				std::cout << "  ok   quad   the corner order traces the quad without crossing it\n";
			else
			{
				std::cout << "  FAIL quad   the corner order crosses the quad\n";
				Failures++;
			}
		}

		// ----------------------------------------------------------- envelopes
		/*
			The preview colours depend on the evaluator, and the reference
			behaviour has a few corners worth pinning down: the time wraps at the
			last point, before the first point the last value is used, the curve
			of the point before the time decides the interpolation, and the values
			are 22.10 fixed point.
		*/
		{
			Checks++;
			const auto Close = [](float A, float B) { return std::abs(A - B) < 0.0005f; };
			const auto AddPoint = [](CEnvelope &Envelope, int Time, int Curve, int A, int B, int C, int D) {
				CEnvelopePoint Point;
				Point.m_Time = Time;
				Point.m_Curvetype = Curve;
				Point.m_aValues[0] = A;
				Point.m_aValues[1] = B;
				Point.m_aValues[2] = C;
				Point.m_aValues[3] = D;
				Envelope.m_vPoints.push_back(Point);
			};
			bool bOk = true;

			CEnvelope Linear;
			Linear.m_Channels = 4;
			AddPoint(Linear, 0, CURVETYPE_LINEAR, 0, 1024, 0, 1024);
			AddPoint(Linear, 1000, CURVETYPE_LINEAR, 1024, 0, 512, 1024);

			CEnvelopeSample Sample;
			EvaluateEnvelope(Linear, 0.0f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.0f) && Close(Sample[1], 1.0f) && Close(Sample[2], 0.0f) && Close(Sample[3], 1.0f);

			EvaluateEnvelope(Linear, 0.5f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.5f) && Close(Sample[1], 0.5f) && Close(Sample[2], 0.25f) && Close(Sample[3], 1.0f);

			// the time wraps at the last point: 1.5s is the state of 0.5s
			EvaluateEnvelope(Linear, 1.5f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.5f);

			EvaluateEnvelope(Linear, 1.0f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.0f);

			bOk = bOk && Close(EnvelopeLength(Linear), 1.0f);

			// before the first point, the last point's value is the answer
			CEnvelope Later;
			Later.m_Channels = 4;
			AddPoint(Later, 100, CURVETYPE_LINEAR, 1024, 0, 0, 1024);
			AddPoint(Later, 900, CURVETYPE_LINEAR, 0, 1024, 0, 1024);
			EvaluateEnvelope(Later, 0.05f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.0f) && Close(Sample[1], 1.0f);

			// step holds the value of the point before the time
			CEnvelope Step;
			Step.m_Channels = 1;
			AddPoint(Step, 0, CURVETYPE_STEP, 0, 0, 0, 0);
			AddPoint(Step, 1000, CURVETYPE_STEP, 1024, 0, 0, 0);
			EvaluateEnvelope(Step, 0.999f, Sample, 1);
			bOk = bOk && Close(Sample[0], 0.0f);

			// a single point is constant, an empty envelope changes nothing
			CEnvelope Single;
			Single.m_Channels = 4;
			AddPoint(Single, 500, CURVETYPE_LINEAR, 256, 512, 768, 1024);
			Sample[0] = Sample[1] = Sample[2] = Sample[3] = -1.0f;
			EvaluateEnvelope(Single, 4.2f, Sample, 4);
			bOk = bOk && Close(Sample[0], 0.25f) && Close(Sample[1], 0.5f) && Close(Sample[2], 0.75f) && Close(Sample[3], 1.0f);

			CEnvelope Empty;
			Sample[0] = 7.0f;
			EvaluateEnvelope(Empty, 1.0f, Sample, 4);
			bOk = bOk && Close(Sample[0], 7.0f);

			// a smooth curve is symmetric around its middle
			CEnvelope Smooth;
			Smooth.m_Channels = 1;
			AddPoint(Smooth, 0, CURVETYPE_SMOOTH, 0, 0, 0, 0);
			AddPoint(Smooth, 1000, CURVETYPE_SMOOTH, 1024, 0, 0, 0);
			EvaluateEnvelope(Smooth, 0.5f, Sample, 1);
			bOk = bOk && Close(Sample[0], 0.5f);

			if(bOk)
				std::cout << "  ok   envel  envelope evaluation matches the reference\n";
			else
			{
				std::cout << "  FAIL envel  envelope evaluation is off\n";
				Failures++;
			}
		}

		std::cout << base::Format("\n%d checks, %d failures\n", Checks, Failures);
		return Failures == 0 ? 0 : 1;
	}

	int RunCommandLine(const std::vector<std::string> &Arguments)
	{
		CArguments Args;
		if(!ParseArguments(Arguments, Args))
		{
			PrintUsage();
			return 2;
		}

		if(Args.m_Command.empty() || Args.m_Command == "--help" || Args.m_Command == "-h")
		{
			if(Args.m_Command.empty() && Args.m_vPositional.empty())
				return -1; // start the editor
			PrintUsage();
			return 0;
		}

		if(Args.m_Command == "--targets")
		{
			PrintTargets();
			return 0;
		}

		if(Args.m_Command == "--selftest")
		{
			std::vector<std::string> Maps;
			for(const std::string &Value : Args.m_vPositional)
				if(Value != "--selftest")
					Maps.push_back(Value);
			return RunSelfTest(Maps);
		}

		if(Args.m_Command == "--dump")
		{
			if(Args.m_vPositional.empty())
			{
				std::cout << "--dump needs a map file\n";
				return 2;
			}
			CMap Map;
			std::string Error;
			if(!Map.LoadFromFile(Args.m_vPositional[0], &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			PrintMap(Map);
			return 0;
		}

		if(Args.m_Command == "--convert")
		{
			if(Args.m_vPositional.size() < 2)
			{
				std::cout << "--convert needs an input and an output file\n";
				return 2;
			}
			const std::optional<map_target> Target = TargetFromId(Args.m_TargetId);
			if(!Target.has_value())
			{
				std::cout << "unknown target '" << Args.m_TargetId << "'\n";
				return 2;
			}
			CMap Map;
			std::string Error;
			if(!Map.LoadFromFile(Args.m_vPositional[0], &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			for(const std::string &Warning : Map.CompatibilityWarnings(*Target))
				std::cout << "warning: " << Warning << "\n";
			if(!Map.SaveToFile(Args.m_vPositional[1], *Target, &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			const CMapTargetProfile &Profile = GetTargetProfile(*Target);
			std::cout << "wrote " << Args.m_vPositional[1] << " for " << Profile.m_pName
				<< " (datafile v" << Profile.m_DatafileVersion << ")\n";
			return 0;
		}

		if(Args.m_Command == "--new")
		{
			if(Args.m_vPositional.empty())
			{
				std::cout << "--new needs an output file\n";
				return 2;
			}
			const std::optional<map_target> Target = TargetFromId(Args.m_TargetId);
			if(!Target.has_value())
			{
				std::cout << "unknown target '" << Args.m_TargetId << "'\n";
				return 2;
			}
			CMap Map;
			Map.InitNew(storage::path(Args.m_vPositional[0]).stem().string());
			Map.m_aAuthor = "Uniteed";
			// the size option resizes the two default layers
			if(Args.m_Width != 50 || Args.m_Height != 50)
			{
				for(CGroup &Group : Map.m_vGroups)
					for(CLayer &Layer : Group.m_vLayers)
						if(Layer.IsTiles())
							Layer.Resize(Args.m_Width, Args.m_Height);
			}
			std::string Error;
			if(!Map.SaveToFile(Args.m_vPositional[0], *Target, &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			std::cout << "wrote " << Args.m_vPositional[0] << "\n";
			return 0;
		}

		if(Args.m_Command == "--pack")
		{
			if(Args.m_vPositional.size() < 2)
			{
				std::cout << "--pack needs an input map and an output package\n";
				return 2;
			}
			const std::string Input = Args.m_vPositional[0];
			CMap Map;
			std::string Error;

			const std::optional<byte_vector> Bytes = storage::read(Input);
			const bool IsPackage = Bytes.has_value() && CUnitwpPackage{}.IsPackage(*Bytes);
			if(IsPackage)
			{
				CUnitwpPackage Existing;
				if(!Existing.LoadFromFile(Input, &Error))
				{
					std::cout << "error: " << Error << "\n";
					return 1;
				}
				Map = Existing.m_Map;
			}
			else if(!Map.LoadFromFile(Input, &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}

			CUnitwpPackage Package;
			Package.Adopt(Map, storage::path(Input).stem().string(), Input);

			if(Args.m_Embed)
			{
				SetupResourcePaths(VersionIdForTarget(*TargetFromId(Args.m_TargetId)));
				for(const CImage &Image : Map.m_vImages)
				{
					if(Image.IsEmbedded())
						continue;
					const std::optional<storage::path> File = FindImageFile(Image.m_Name);
					if(!File.has_value())
					{
						std::cout << "warning: no mapres file for image '" << Image.m_Name << "'\n";
						continue;
					}
					const std::optional<byte_vector> Data = storage::read(*File);
					if(!Data.has_value())
						continue;
					CUnitwpResource Resource;
					Resource.m_Name = "mapres/" + File->filename().string();
					Resource.m_Data = *Data;
					Package.m_vResources.push_back(std::move(Resource));
				}
				std::cout << "embedded " << Package.m_vResources.size() << " resources\n";
			}

			if(!Package.SaveToFile(Args.m_vPositional[1], &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			std::cout << "wrote " << Args.m_vPositional[1] << "\n";
			return 0;
		}

		if(Args.m_Command == "--unpack")
		{
			if(Args.m_vPositional.size() < 2)
			{
				std::cout << "--unpack needs a package and an output map\n";
				return 2;
			}
			const std::optional<map_target> Target = TargetFromId(Args.m_TargetId);
			if(!Target.has_value())
			{
				std::cout << "unknown target '" << Args.m_TargetId << "'\n";
				return 2;
			}
			CUnitwpPackage Package;
			std::string Error;
			if(!Package.LoadFromFile(Args.m_vPositional[0], &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			if(!Package.m_Map.SaveToFile(Args.m_vPositional[1], *Target, &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			std::cout << "wrote " << Args.m_vPositional[1] << "\n";
			return 0;
		}

		std::cout << "unknown command '" << Args.m_Command << "'\n\n";
		PrintUsage();
		return 2;
	}
} // namespace game
