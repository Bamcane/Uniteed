#include "cli.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

#include "base/define.h"
#include "base/json.h"
#include "base/i18n.h"
#include "base/logger.h"
#include "base/math.h"
#include "base/png.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/chrome.h"
#include "engine/keycodes.h"
#include "engine/svg.h"
#include "game/assets.h"
#include "game/editor/editor.h"
#include "game/editor/render.h"
#include "game/map/envelope.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"
#include "game/project.h"
#include "game/rules/rules.h"

namespace game
{
	namespace i18n = base::i18n;
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
				"  --render <map> <out.png> [--center X,Y] [--size WxH] [--zoom Z] [--time S]\n"
				"                                  [--transparent] [--background RRGGBB]\n"
				"                                  draw the map's tiles and quads to a PNG, no window\n"
				"  --targets                       print the version matrix\n"
				"  --selftest [map ...]            round trip every bundled map\n"
				"  --agent                         a resident JSON session on stdin and stdout\n"
				"  --agent-describe                the agent method catalogue\n"
				"  --help\n"
				"\n"
				"Every administrative method is also a subcommand, with --key value parameters:\n"
				"  uniteed project.create --name demo --target ddnet07\n"
				"  uniteed tiles.set --group 1 --layer 0 --x 4 --y 4 --index 3\n"
				"  uniteed export.map --target ddnet07\n"
				"\n"
				"target ids: 06 (Teeworlds 0.6), 07 (0.7), ddnet (DDRaceNetwork, the 0.6\n"
				"flavour), ddnet06, ddnet07, 08 (0.8, datafile v5)\n"
				"\n"
				"projects live in <data home>/uniteed/projects, exports in <data home>/uniteed/export;\n"
				"set UNITEED_PROJECTS_DIR or UNITEED_EXPORT_DIR to move either of them\n";
		}

		void PrintTargets()
		{
			std::cout << "target  datafile  tilemap  image  group  envelopes  notes\n";
			for(const map_target Target : AllTargets())
			{
				const CMapTargetProfile &Profile = GetTargetProfile(Target);
				std::cout << base::Format("{:<7} {:<9d} {:<8d} {:<6d} {:<6d} {:<10} {}\n",
					Profile.m_pId, Profile.m_DatafileVersion, Profile.m_TilemapVersion,
					Profile.m_ImageVersion, Profile.m_GroupVersion,
					Profile.HasExtension(ext_bezier_envelopes) ? "v2/v3" : "v2",
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
			bool m_bSizeSet = false;
			// --render: where to look, how far in, and at which envelope time
			base::vec2 m_Center;
			bool m_bCenterSet = false;
			float m_Zoom = 1.0f;
			float m_Time = 0.0f;
			bool m_bTransparent = false;
			CColor m_Background{24, 26, 30, 255};
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
				else if(Argument == "--center")
				{
					if(i + 1 >= Arguments.size())
						return false;
					const std::string Value = Arguments[++i];
					const std::size_t Comma = Value.find(',');
					if(Comma == std::string::npos)
						return false;
					Out.m_Center.x = static_cast<float>(std::atof(Value.substr(0, Comma).c_str()));
					Out.m_Center.y = static_cast<float>(std::atof(Value.substr(Comma + 1).c_str()));
					Out.m_bCenterSet = true;
				}
				else if(Argument == "--zoom")
				{
					if(i + 1 >= Arguments.size())
						return false;
					Out.m_Zoom = static_cast<float>(std::atof(Arguments[++i].c_str()));
				}
				else if(Argument == "--time")
				{
					if(i + 1 >= Arguments.size())
						return false;
					Out.m_Time = static_cast<float>(std::atof(Arguments[++i].c_str()));
				}
				else if(Argument == "--transparent")
				{
					Out.m_bTransparent = true;
				}
				else if(Argument == "--background")
				{
					if(i + 1 >= Arguments.size())
						return false;
					std::string Value = Arguments[++i];
					if(!Value.empty() && Value[0] == '#')
						Value = Value.substr(1);
					const unsigned long Rgb = std::strtoul(Value.c_str(), nullptr, 16);
					Out.m_Background = CColor{static_cast<int>((Rgb >> 16) & 0xFF),
						static_cast<int>((Rgb >> 8) & 0xFF), static_cast<int>(Rgb & 0xFF), 255};
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
					Out.m_bSizeSet = true;
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
			std::cout << base::Format("datafile v{:d}  groups={:d} layers={:d} images={:d}({:d} embedded) envelopes={:d} quads={:d} preserved items={:d}\n",
				Map.m_SourceDatafileVersion, Stats.m_Groups, Stats.m_Layers, Stats.m_Images,
				Stats.m_EmbeddedImages, Stats.m_Envelopes, Stats.m_Quads, Stats.m_Extras);
			std::cout << base::Format("info: author='{}' version='{}' credits='{}' license='{}'\n",
				Map.m_aAuthor.c_str(), Map.m_aVersion.c_str(), Map.m_aCredits.c_str(), Map.m_aLicense.c_str());
			std::cout << base::Format("tiles={:d} non-empty={:d}\n", Stats.m_Tiles, Stats.m_TilesWithContent);

			for(std::size_t g = 0; g < Map.m_vGroups.size(); g++)
			{
				const CGroup &Group = Map.m_vGroups[g];
				const std::string Clip = Group.m_UseClipping ?
					base::Format("{:d},{:d} {:d}x{:d}", Group.m_ClipX, Group.m_ClipY, Group.m_ClipW, Group.m_ClipH) : "off";
				std::cout << base::Format("  group {:d} '{}' parallax={:d},{:d} offset={:d},{:d} clip={} version={:d} layers={:d}\n",
					g, Group.m_Name.c_str(), Group.m_ParallaxX, Group.m_ParallaxY, Group.m_OffsetX, Group.m_OffsetY,
					Clip.c_str(), Group.m_SourceVersion, Group.m_vLayers.size());
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
						Extra = base::Format(" {:d}x{:d} flags={:d} color={:d},{:d},{:d},{:d} image={:d} non-empty={:d} tilemap-version={:d}{}",
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
						// a quad that carries a position envelope is drawn
						// somewhere other than where it is stored, which is
						// worth seeing without opening the editor
						int PositionAnimated = 0, ColorAnimated = 0;
						for(const CQuad &Quad : Layer.m_vQuads)
						{
							if(Quad.m_PosEnv >= 0)
								PositionAnimated++;
							if(Quad.m_ColorEnv >= 0)
								ColorAnimated++;
						}
						Extra = base::Format(" quads={:d} graded={:d} image={:d} pos-env={:d} color-env={:d}",
							Layer.m_vQuads.size(), Graded, Layer.m_Image, PositionAnimated, ColorAnimated);
					}
					else if(Layer.IsSounds())
						Extra = base::Format(" sources={:d} sound={:d}", Layer.m_vSources.size(), Layer.m_Sound);
					if(Layer.m_ColorEnv >= 0)
						Extra += base::Format(" color-env={:d}+{:d}", Layer.m_ColorEnv, Layer.m_ColorEnvOffset);
					std::cout << base::Format("    layer {:d} '{}' type={}{}\n", l, Layer.m_Name.c_str(), pKind, Extra.c_str());
				}
			}

			for(std::size_t i = 0; i < Map.m_vImages.size(); i++)
			{
				const CImage &Image = Map.m_vImages[i];
				std::cout << base::Format("  image {:d} '{}' {:d}x{:d} {}\n", i, Image.m_Name.c_str(), Image.m_Width, Image.m_Height,
					Image.IsEmbedded() ? "embedded" : "external");
			}
			for(std::size_t i = 0; i < Map.m_vEnvelopes.size(); i++)
			{
				const CEnvelope &Envelope = Map.m_vEnvelopes[i];
				std::cout << base::Format("  envelope {:d} '{}' channels={:d} points={:d} synchronized={:d}\n",
					i, Envelope.m_Name.c_str(), Envelope.m_Channels, Envelope.m_vPoints.size(), Envelope.m_Synchronized);
				for(std::size_t p = 0; p < Envelope.m_vPoints.size(); p++)
				{
					const CEnvelopePoint &Point = Envelope.m_vPoints[p];
					std::cout << base::Format("    point {:d} t={:d} curve={} values={:d},{:d},{:d},{:d}\n", p, Point.m_Time,
						Point.m_Curvetype == CURVETYPE_STEP ? "step" : Point.m_Curvetype == CURVETYPE_SLOW ? "slow" :
						Point.m_Curvetype == CURVETYPE_FAST ? "fast" : Point.m_Curvetype == CURVETYPE_SMOOTH ? "smooth" :
						Point.m_Curvetype == CURVETYPE_BEZIER ? "bezier" : "linear",
						Point.m_aValues[0], Point.m_aValues[1], Point.m_aValues[2], Point.m_aValues[3]);
				}
			}
			for(const CExtraItem &Extra : Map.m_vExtras)
				std::cout << base::Format("  preserved item type={:d} id={:d} size={:d}\n", Extra.m_Type, Extra.m_ID, Extra.m_Data.size());
		}

		// structural comparison of two models, used by the selftest
		/*
			bExpectExtras says whether this target is supposed to keep the items
			the editor does not model. A vanilla target may legitimately leave
			them out - the export warns about that - so the comparison is told
			what to expect instead of guessing from the maps themselves.
		*/
		bool CompareMaps(const CMap &A, const CMap &B, std::string &Error, bool bExpectExtras = true)
		{
			const CMapStatistics StatsA = A.Statistics();
			const CMapStatistics StatsB = B.Statistics();
			if(StatsA.m_Images != StatsB.m_Images)
			{
				Error = base::Format("image count {:d} -> {:d}", StatsA.m_Images, StatsB.m_Images);
				return false;
			}
			for(std::size_t i = 0; i < A.m_vImages.size(); i++)
			{
				if(A.m_vImages[i].m_Name != B.m_vImages[i].m_Name)
				{
					Error = base::Format("image {:d} name '{}' -> '{}'", i, A.m_vImages[i].m_Name.c_str(), B.m_vImages[i].m_Name.c_str());
					return false;
				}
				if(A.m_vImages[i].m_Width != B.m_vImages[i].m_Width || A.m_vImages[i].m_Height != B.m_vImages[i].m_Height)
				{
					Error = base::Format("image {:d} dimensions changed", i);
					return false;
				}
				if(A.m_vImages[i].m_Data != B.m_vImages[i].m_Data)
				{
					Error = base::Format("image {:d} pixel data changed", i);
					return false;
				}
			}
			/*
				The preserved items are part of the map even though the editor
				does not model them: losing one silently is exactly the bug this
				comparison exists to catch, so compare them by type, id and body.
			*/
			if(!bExpectExtras)
			{
				// nothing to compare: the target does not carry them at all
			}
			else if(A.m_vExtras.size() != B.m_vExtras.size())
			{
				Error = base::Format("preserved item count {:d} -> {:d}", A.m_vExtras.size(), B.m_vExtras.size());
				return false;
			}
			for(std::size_t i = 0; bExpectExtras && i < A.m_vExtras.size() && i < B.m_vExtras.size(); i++)
			{
				if(A.m_vExtras[i].m_Type != B.m_vExtras[i].m_Type || A.m_vExtras[i].m_ID != B.m_vExtras[i].m_ID)
				{
					Error = base::Format("preserved item {:d} changed type/id ({:d}/{:d} -> {:d}/{:d})", i,
						A.m_vExtras[i].m_Type, A.m_vExtras[i].m_ID, B.m_vExtras[i].m_Type, B.m_vExtras[i].m_ID);
					return false;
				}
				if(A.m_vExtras[i].m_Data != B.m_vExtras[i].m_Data)
				{
					Error = base::Format("preserved item {:d} (type {:d}) changed body", i, A.m_vExtras[i].m_Type);
					return false;
				}
			}
			if(A.m_vEnvelopes.size() != B.m_vEnvelopes.size())
			{
				Error = base::Format("envelope count {:d} -> {:d}", A.m_vEnvelopes.size(), B.m_vEnvelopes.size());
				return false;
			}
			for(std::size_t i = 0; i < A.m_vEnvelopes.size(); i++)
			{
				if(A.m_vEnvelopes[i].m_vPoints.size() != B.m_vEnvelopes[i].m_vPoints.size())
				{
					Error = base::Format("envelope {:d} point count changed", i);
					return false;
				}
				for(std::size_t p = 0; p < A.m_vEnvelopes[i].m_vPoints.size(); p++)
				{
					const CEnvelopePoint &PA = A.m_vEnvelopes[i].m_vPoints[p];
					const CEnvelopePoint &PB = B.m_vEnvelopes[i].m_vPoints[p];
					if(PA.m_Time != PB.m_Time || PA.m_Curvetype != PB.m_Curvetype ||
						std::memcmp(PA.m_aValues, PB.m_aValues, sizeof(PA.m_aValues)) != 0)
					{
						Error = base::Format("envelope {:d} point {:d} changed", i, p);
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
						Error = base::Format("group {:d} layer {:d} identity changed", g, l);
						return false;
					}
					if(LA.IsTiles())
					{
						if(LA.m_Width != LB.m_Width || LA.m_Height != LB.m_Height)
						{
							Error = base::Format("group {:d} layer {:d} dimensions changed", g, l);
							return false;
						}
						// the layer colour and its envelope have to survive too
						if(LA.m_Color.r != LB.m_Color.r || LA.m_Color.g != LB.m_Color.g ||
							LA.m_Color.b != LB.m_Color.b || LA.m_Color.a != LB.m_Color.a)
						{
							Error = base::Format("group {:d} layer {:d} colour changed ({:d},{:d},{:d},{:d} -> {:d},{:d},{:d},{:d})", g, l,
								LA.m_Color.r, LA.m_Color.g, LA.m_Color.b, LA.m_Color.a,
								LB.m_Color.r, LB.m_Color.g, LB.m_Color.b, LB.m_Color.a);
							return false;
						}
						if(LA.m_ColorEnv != LB.m_ColorEnv || LA.m_ColorEnvOffset != LB.m_ColorEnvOffset)
						{
							Error = base::Format("group {:d} layer {:d} colour envelope changed", g, l);
							return false;
						}
						if(LA.IsPhysicsLayer())
							continue;
						if(LA.m_vTiles.size() != LB.m_vTiles.size())
						{
							Error = base::Format("group {:d} layer {:d} tile count changed", g, l);
							return false;
						}
						for(std::size_t t = 0; t < LA.m_vTiles.size(); t++)
						{
							if(LA.m_vTiles[t].m_Index != LB.m_vTiles[t].m_Index || LA.m_vTiles[t].m_Flags != LB.m_vTiles[t].m_Flags)
							{
								Error = base::Format("group {:d} layer {:d} tile {:d} changed ({:d}/{:d} -> {:d}/{:d})", g, l, t,
									LA.m_vTiles[t].m_Index, LA.m_vTiles[t].m_Flags, LB.m_vTiles[t].m_Index, LB.m_vTiles[t].m_Flags);
								return false;
							}
						}
					}
					else if(LA.IsQuads())
					{
						if(LA.m_vQuads.size() != LB.m_vQuads.size())
						{
							Error = base::Format("group {:d} layer {:d} quad count changed", g, l);
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
									Error = base::Format("group {:d} layer {:d} quad {:d} corner {:d} colour changed", g, l, q, c);
									return false;
								}
							}
							if(LA.m_vQuads[q].m_ColorEnv != LB.m_vQuads[q].m_ColorEnv ||
								LA.m_vQuads[q].m_PosEnv != LB.m_vQuads[q].m_PosEnv)
							{
								Error = base::Format("group {:d} layer {:d} quad {:d} envelope changed", g, l, q);
								return false;
							}
						}
					}
					else if(LA.IsSounds() && LA.m_vSources.size() != LB.m_vSources.size())
					{
						Error = base::Format("group {:d} layer {:d} sound source count changed", g, l);
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

		/*
			The string builder of the interface. It is std::format with "{}"
			fields now, so a call is checked against its arguments when it is
			compiled; a format string that is only known while the program runs
			goes through the unchecked variant, which has to return something
			readable rather than throw in the middle of a frame.
		*/
		{
			Checks++;
			const std::string Real = base::Format("{:d} of {} = {:.2f}", 2, std::string("five"), 2.5);
			const std::string Padded = base::Format("[{:>4d}][{:<4d}]", 7, 7);
			bool bOk = Real == "2 of five = 2.50" && Padded == "[   7][7   ]";
			// the runtime variant is what a translated format string goes
			// through, and it agrees with the checked one
			const std::string_view Runtime = "{:d} of {} = {:.2f}";
			bOk = bOk && base::FormatRuntime(Runtime, 2, std::string("five"), 2.5) == Real;
			// a format string that does not match its arguments cannot be a
			// build error here: it leaves the string on screen, and what the
			// check pins down is that nothing was thrown on the way
			bOk = bOk && base::FormatRuntime("{:d}", "not a number") == "{:d}";

			if(bOk)
				std::cout << "\n  ok   format strings are built with std::format\n";
			else
			{
				std::cout << "\n  FAIL the string builder changed behaviour\n";
				Failures++;
			}
		}

		/*
			The language catalogs under <data>/lang, and the lookup the interface
			makes for every string it draws. What the check pins down is what a
			catalog has to satisfy - an id, a name and strings under it - and
			that everything else stays readable: a string no catalog carries
			comes out as the English source rather than as an empty label, and a
			language that is not installed is refused instead of half applied.
		*/
		{
			Checks++;
			bool bOk = true;
			// English is the source text, so it is always installed and needs
			// no file to be usable
			i18n::RefreshLanguages();
			bOk = bOk && i18n::Languages().size() >= 2 && i18n::Languages().front().m_Id == "en";
			bOk = bOk && i18n::SetLanguage("en") && i18n::CurrentId() == "en" && i18n::EntryCount() == 0;
			bOk = bOk && std::string(i18n::Tr("Files")) == "Files";

			bOk = bOk && i18n::SetLanguage("zh-Hans") && i18n::CurrentId() == "zh-Hans";
			bOk = bOk && i18n::EntryCount() > 300;
			bOk = bOk && std::string(i18n::Tr("Files")) != "Files";
			/*
				A string no catalog carries, and a missing literal, stay
				readable. The string comes from a variable on purpose: a
				literal would be a key tools/lang.py asks the catalog for.
			*/
			const std::string Unknown = "a string no catalog carries";
			bOk = bOk && i18n::Tr(Unknown.c_str()) == Unknown;
			bOk = bOk && i18n::Tr(nullptr) == nullptr;
			// an id that is not installed leaves the language where it is
			bOk = bOk && !i18n::SetLanguage("xx-YY") && i18n::CurrentId() == "zh-Hans";
			// and switching back really switches back
			bOk = bOk && i18n::SetLanguage("en") && std::string(i18n::Tr("Files")) == "Files";

			if(bOk)
				std::cout << "\n  ok   i18n  the catalog under data/lang loads and falls back to the source\n";
			else
			{
				std::cout << "\n  FAIL the interface language changed behaviour\n";
				Failures++;
			}
		}

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

				if(!CompareMaps(Source, Reloaded, Error, Profile.HasExtension(ext_extras)))
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
				if(!Twice.LoadFromBytes(Again, &Error) || !CompareMaps(Source, Twice, Error, Profile.HasExtension(ext_extras)))
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

				std::cout << base::Format("  ok   {:<6} {:6d} bytes -> reload ok, stable{}\n",
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
						std::cout << base::Format("  ok   {:<6} {:6d} bytes -> package round trip ok\n", "unitwp", Packed.size());
					}
				}
			}

			/*
				A Uniteed project is the same container, and the point of it is
				that the editor state and the autorules come back exactly as they
				went in - a project that loses its camera or its rules is not a
				project. The check is in memory, so it does not depend on where
				the application data directory happens to be.
			*/
			{
				Checks++;
				bool bOk = true;
				CProject Project;
				Project.InitNew("selftest-project", "ddnet07");
				Project.m_Map = Source;
				Project.m_Meta.m_Author = "unit test";
				Project.m_Meta.m_Description = "round trip";
				Project.m_Meta.m_Target = "ddnet06";
				Project.m_Meta.m_Assets = "ddnet";
				// a distinctive editor state, so a dropped or defaulted key shows
				base::json Editor = base::json::object();
				Editor["zoom"] = 3.5;
				Editor["center_x"] = 1234.5;
				Editor["grid"] = true;
				Editor["allow_extensions"] = true;
				Editor["brush_size"] = 4;
				Editor["a_key_from_a_newer_build"] = "carried over";
				Project.m_Editor = std::move(Editor);

				rules::CRuleSet Rules;
				Rules.m_Name = "selftest rules";
				rules::CConfiguration Config;
				Config.m_Name = "config";
				rules::CRun Run;
				Run.m_ReadSlot = 6;
				rules::CIndexRule Rule;
				Rule.m_Tile = 3;
				Run.m_vIndexRules.push_back(Rule);
				Config.m_vRuns.push_back(Run);
				Rules.m_vConfigs.push_back(Config);
				Project.m_Rules = rules::ToJson(Rules);

				byte_vector Bytes;
				std::string Error;
				CProject Back;
				if(!Project.SaveToBytes(Bytes, &Error))
				{
					std::cout << "  FAIL unitwp project: " << Error << "\n";
					bOk = false;
				}
				else if(!CUnitwpPackage::IsPackage(Bytes) || std::memcmp(Bytes.data(), "UNITWP", 6) != 0)
					bOk = false;
				else if(!Back.LoadFromBytes(Bytes, &Error))
					bOk = false;
				else
				{
					const base::json &BackEditor = Back.m_Editor;
					bOk = bOk && Back.m_Meta.m_Name == "selftest-project" &&
						Back.m_Meta.m_Author == "unit test" &&
						Back.m_Meta.m_Description == "round trip" &&
						Back.m_Meta.m_Target == "ddnet06" &&
						Back.m_Meta.m_Assets == "ddnet" &&
						Back.m_Editor["zoom"].as_double(0.0) == 3.5 &&
						Back.m_Editor["center_x"].as_double(0.0) == 1234.5 &&
						Back.m_Editor["grid"].as_bool(false) &&
						Back.m_Editor["allow_extensions"].as_bool(false) &&
						Back.m_Editor["brush_size"].as_int(0) == 4 &&
						BackEditor["a_key_from_a_newer_build"].as_string() == "carried over" &&
						Back.m_Rules["autorules"].as_int(0) > 0 &&
						Back.m_Rules["name"].as_string() == "selftest rules" &&
						Back.m_Map.m_vGroups.size() == Source.m_vGroups.size();
					// the unknown key is folded back in when the project is saved
					base::json Merged = Back.MergeEditorState(base::json::object());
					bOk = bOk && Merged["a_key_from_a_newer_build"].as_string() == "carried over";
				}
				if(bOk)
					std::cout << "  ok   unitwp a project keeps its meta, its session and its rules\n";
				else
				{
					std::cout << "  FAIL unitwp a project did not round trip\n";
					Failures++;
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
						Why = base::Format("{}: {}", GetTargetProfile(Target).m_pId, Error.c_str());
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
						Why = base::Format("{} lost the {} colour", GetTargetProfile(Target).m_pId,
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

		// ------------------------------------------------------------- groups
		/*
			A group is drawn through its parallax and offset, the reference
			mapping is center * parallax/100 + offset with the viewport size
			unchanged. A parallax of 100 and no offset has to be the map view
			itself, or the level would move with the backgrounds.
		*/
		{
			Checks++;
			CMap Map;
			Map.InitNew("06");
			const CGroup &Game = Map.m_vGroups.front();
			base::view_transform View;
			View.m_Origin = base::vec2(268.0f, 40.0f);
			View.m_Viewport = base::vec2(1064.0f, 834.0f);
			View.m_Center = base::vec2(960.0f, 800.0f);
			View.m_Zoom = 0.5f;

			// the game group is parallax 100 with no offset
			const base::view_transform GameView = GroupView(View, Game);
			bool bOk = std::abs(GameView.m_Center.x - View.m_Center.x) < 0.001f &&
				std::abs(GameView.m_Center.y - View.m_Center.y) < 0.001f;

			// half the parallax puts the group centre at half the camera
			CGroup Half = Game;
			Half.m_ParallaxX = 50;
			Half.m_ParallaxY = 50;
			Half.m_OffsetX = 100;
			Half.m_OffsetY = -40;
			const base::view_transform HalfView = GroupView(View, Half);
			bOk = bOk && std::abs(HalfView.m_Center.x - (960.0f * 0.5f + 100.0f)) < 0.001f &&
				std::abs(HalfView.m_Center.y - (800.0f * 0.5f - 40.0f)) < 0.001f;

			// the viewport size does not change with the parallax
			bOk = bOk && std::abs(HalfView.m_Viewport.x - View.m_Viewport.x) < 0.001f &&
				std::abs(HalfView.m_Viewport.y - View.m_Viewport.y) < 0.001f;

			if(bOk)
				std::cout << "  ok   group  the parallax and offset place a group's view\n";
			else
			{
				std::cout << "  FAIL group  the parallax and offset are applied wrongly\n";
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
			/*
				A quad is drawn as a grid of cells whose corners are placed by
				(u,v), and that walk has to be the same perimeter. Routing those
				points through the stored corner order a second time crossed
				every cell into a bowtie, which is what showed up as black
				triangles tiled across the sky.
			*/
			const int aByUV[4] = {CQuad::s_aCornerAt[0][0], CQuad::s_aCornerAt[1][0],
				CQuad::s_aCornerAt[1][1], CQuad::s_aCornerAt[0][1]};
			for(int i = 0; i < 4; i++)
				if(aByUV[i] != CQuad::s_aPerimeter[i])
					bOk = false;

			if(bOk)
				std::cout << "  ok   quad   the corner order traces the quad without crossing it\n";
			else
			{
				std::cout << "  FAIL quad   the corner order crosses the quad\n";
				Failures++;
			}
		}

		// --------------------------------------------------- quad at frame time
		/*
			A quad is picked where the position envelope has moved it, not where
			it is stored. The editor draws the animated points, so testing the
			stored ones would select a quad that is not under the cursor - the
			exact complaint that a quad could be clicked but was "not there".

			The check builds a quad whose position envelope translates it by a
			whole quad width, then confirms that the animated point of the
			first frame is inside the animated outline and the stored point is
			not: the two disagree, which is what makes the test meaningful.
		*/
		{
			Checks++;
			CMap Map;
			Map.InitNew("quad-env");
			CGroup &Group = Map.m_vGroups.back();
			CLayer &Layer = Map.NewQuadLayer(Group, -1);
			Layer.m_vQuads.emplace_back();
			CQuad &Quad = Layer.m_vQuads.back();
			// a 100 by 100 quad at the origin, top left / top right / bottom
			// left / bottom right in the stored order
			const int aCorners[4][2] = {{0, 0}, {100, 0}, {0, 100}, {100, 100}};
			for(int i = 0; i < 4; i++)
			{
				Quad.m_aPoints[i].x = aCorners[i][0] * 1024;
				Quad.m_aPoints[i].y = aCorners[i][1] * 1024;
			}

			// a position envelope that moves the quad by 1000 units in x over
			// one second, so the animated and the stored quad do not overlap
			CEnvelope Envelope;
			Envelope.m_Channels = 3;
			CEnvelopePoint A;
			A.m_Time = 0;
			A.m_Curvetype = CURVETYPE_LINEAR;
			A.m_aValues[0] = 0;
			A.m_aValues[1] = 0;
			A.m_aValues[2] = 0;
			CEnvelopePoint B;
			B.m_Time = 1000;
			B.m_Curvetype = CURVETYPE_LINEAR;
			B.m_aValues[0] = 1000 * 1024;
			B.m_aValues[1] = 0;
			B.m_aValues[2] = 0;
			Envelope.m_vPoints.push_back(A);
			Envelope.m_vPoints.push_back(B);
			Map.m_vEnvelopes.push_back(Envelope);
			Quad.m_PosEnv = static_cast<int>(Map.m_vEnvelopes.size()) - 1;

			// the animated points at 0.5s: the envelope has moved the quad half
			// way, so its outline is [500, 600] x [0, 100] and the stored point
			// (50, 50) is outside it
			CEnvelopeSample Sample;
			EvaluateEnvelope(Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_PosEnv)], 0.5f, Sample, 3);
			const base::vec2 Offset(Sample[0], Sample[1]);
			base::vec2 aAnimated[4];
			for(int i = 0; i < 4; i++)
			{
				aAnimated[i] = base::vec2(static_cast<float>(aCorners[i][0]), static_cast<float>(aCorners[i][1])) + Offset;
			}

			// the test the editor uses for the hover, on both point sets: the
			// corners go in in perimeter order, the stored order traces a
			// bowtie and would report the wrong half
			base::vec2 aAnimatedOutline[4];
			base::vec2 aStoredOutline[4];
			for(int i = 0; i < 4; i++)
			{
				aAnimatedOutline[i] = aAnimated[CQuad::s_aPerimeter[i]];
				aStoredOutline[i] = base::vec2(static_cast<float>(aCorners[CQuad::s_aPerimeter[i]][0]),
					static_cast<float>(aCorners[CQuad::s_aPerimeter[i]][1]));
			}
			const bool bHitsAnimated = base::PointInQuad(aAnimatedOutline, base::vec2(550.0f, 50.0f));
			const bool bHitsStored = base::PointInQuad(aStoredOutline, base::vec2(550.0f, 50.0f));

			bool bOk = bHitsAnimated && !bHitsStored;
			// and the offset is really the envelope's, not a constant
			bOk = bOk && std::abs(Offset.x - 500.0f) < 1.0f && std::abs(Offset.y) < 0.001f;
			// the stored point of the same quad is inside the stored outline,
			// so the two sets really do disagree about this point
			bOk = bOk && base::PointInQuad(aStoredOutline, base::vec2(50.0f, 50.0f));

			if(bOk)
				std::cout << "  ok   quad   a quad is picked where its envelope has moved it\n";
			else
			{
				std::cout << "  FAIL quad   a quad is picked where it is stored, not where it is drawn\n";
				Failures++;
			}
		}

		// --------------------------------------------------------- rule engine
		/*
			The autorules engine is the one part of the editor that turns user
			authored data into tiles without anybody clicking, so it is pinned
			down with a rule whose outcome is unambiguous: a hookable tile is
			written wherever the game layer has a death tile to its left.

			It also pins the two things that are easy to get wrong: the modular
			and positional conditions are ANDed rather than ORed, and the same
			seed on the same input produces the same output, which is what the
			command line and any scripted run rely on.
		*/
		{
			Checks++;
			rules::CRuleSet Rules;
			Rules.m_Name = "selftest";
			rules::CConfiguration Config;
			Config.m_Name = "config";
			rules::CRun Run;
			Run.m_ReadSlot = 6;
			Run.m_bCopy = true;
			{
				rules::CIndexRule Rule;
				Rule.m_Tile = 1;
				Rule.m_Name = "hookable";
				rules::CPosRule When;
				When.m_X = 1;
				When.m_Mode = rules::pos_rule_mode::index;
				rules::CIndexInfo Info;
				Info.m_Tile = 2; // death
				When.m_vIndices.push_back(Info);
				Rule.m_vRules.push_back(When);
				// only on even columns, and never on an empty read cell
				rules::CModuloRule Modulo;
				Modulo.m_ModX = 2;
				Modulo.m_ModY = 1;
				Rule.m_vModuloRules.push_back(Modulo);
				// Nothing is skipped: an empty read tile still has to be
				// written at (2,0), and the conditions above decide where the
				// rule fires, not the emptiness of the cell it writes into.
				Rule.m_bSkipEmpty = false;
				Run.m_vIndexRules.push_back(Rule);
			}
			Config.m_vRuns.push_back(Run);
			Rules.m_vConfigs.push_back(Config);

			constexpr int Width = 8, Height = 1;
			std::vector<std::uint8_t> ReadTiles(Width * Height, 0), ReadFlags(Width * Height, 0);
			std::vector<std::uint8_t> TargetTiles(Width * Height, 0), TargetFlags(Width * Height, 0);
			/*
				Death at (3,0) and (5,0). The rule fires at (2,0) and (4,0),
				because in both cases the cell to the right is death and the
				column is even; it does not fire at (0,0) or (6,0), which are
				even but have no death to the right, nor at (3,0), which is odd.
			*/
			ReadTiles[3] = 2;
			ReadTiles[5] = 2;

			const rules::CRuleLayer Target{TargetTiles.data(), TargetFlags.data(), Width, Height};
			std::vector<rules::CRuleLayer> Read;
			Read.resize(7);
			Read[6] = rules::CRuleLayer{ReadTiles.data(), ReadFlags.data(), Width, Height};

			const rules::CApplyResult First = rules::Apply(Rules, 0, Target, Read, 12345);
			bool bOk = First.m_bOk && TargetTiles[2] == 1 && TargetTiles[4] == 1 &&
				TargetTiles[0] == 0 && TargetTiles[1] == 0 && TargetTiles[3] == 0 &&
				TargetTiles[5] == 0 && TargetTiles[6] == 0 && TargetTiles[7] == 0;

			// the same seed and the same input are the same output
			std::vector<std::uint8_t> AgainTiles(Width * Height, 0), AgainFlags(Width * Height, 0);
			const rules::CRuleLayer Again{AgainTiles.data(), AgainFlags.data(), Width, Height};
			const rules::CApplyResult Second = rules::Apply(Rules, 0, Again, Read, 12345);
			bOk = bOk && Second.m_bOk && AgainTiles == TargetTiles;

			// a rule set with no read layer fails closed rather than writing
			std::vector<rules::CRuleLayer> Empty;
			std::vector<std::uint8_t> NoReadTiles(Width * Height, 0), NoReadFlags(Width * Height, 0);
			const rules::CRuleLayer NoRead{NoReadTiles.data(), NoReadFlags.data(), Width, Height};
			const rules::CApplyResult Missing = rules::Apply(Rules, 0, NoRead, Empty, 7);
			bOk = bOk && Missing.m_bOk && NoReadTiles[2] == 0;

			// the JSON round trip, which is what a project stores
			std::string JsonError;
			const rules::CRuleSet Back = rules::FromJson(rules::ToJson(Rules), &JsonError);
			bOk = bOk && JsonError.empty() && Back.m_vConfigs.size() == 1 && Back.RuleCount() == 1 &&
				Back.m_vConfigs[0].m_vRuns[0].m_vIndexRules[0].m_vRules.size() == 1 &&
				Back.m_vConfigs[0].m_vRuns[0].m_vIndexRules[0].m_vModuloRules.size() == 1;

			if(bOk)
				std::cout << "  ok   rules  a rule set writes where its conditions hold, and repeats\n";
			else
			{
				std::cout << "  FAIL rules  the rule engine or its JSON round trip is off\n";
				Failures++;
			}
		}

		// ------------------------------------------------- bezier envelope export
		/*
			The requirement this pins: a custom curve is written the Teeworlds
			way - version 3 envelope points with the tangents packed into the 88
			byte point - for *every* target, including DDRaceNetwork, which has a
			separate UUID item for them. Anything else would make a curve depend
			on which client the map was exported for.
		*/
		{
			Checks++;
			CMap Map;
			Map.InitNew("bezier");
			CEnvelope Envelope;
			Envelope.m_Channels = 4;
			Envelope.m_Name = "curve";
			CEnvelopePoint A;
			A.m_Time = 0;
			A.m_Curvetype = CURVETYPE_LINEAR;
			for(int c = 0; c < 4; c++)
				A.m_aValues[c] = c * 100;
			CEnvelopePoint B;
			B.m_Time = 1500;
			B.m_Curvetype = CURVETYPE_BEZIER;
			for(int c = 0; c < 4; c++)
				B.m_aValues[c] = 1024 - c * 100;
			// distinctive tangents, so a dropped or reordered tangent is visible
			for(int c = 0; c < 4; c++)
			{
				B.m_Bezier.m_aInTangentdx[c] = -100 * (c + 1);
				B.m_Bezier.m_aInTangentdy[c] = -200 * (c + 1);
				B.m_Bezier.m_aOutTangentdx[c] = 300 * (c + 1);
				B.m_Bezier.m_aOutTangentdy[c] = 400 * (c + 1);
			}
			Envelope.m_vPoints.push_back(A);
			Envelope.m_vPoints.push_back(B);
			Map.m_vEnvelopes.push_back(Envelope);

			bool bOk = true;
			std::string Why;
			for(const map_target Target : AllTargets())
			{
				byte_vector Bytes;
				CMap Back;
				std::string Error;
				if(!Map.SaveToBytes(Bytes, Target, &Error) || !Back.LoadFromBytes(Bytes, &Error))
				{
					bOk = false;
					Why = base::Format("{}: {}", GetTargetProfile(Target).m_pId, Error.c_str());
					break;
				}
				if(Back.m_vEnvelopes.size() != 1 || Back.m_vEnvelopes[0].m_vPoints.size() != 2)
				{
					bOk = false;
					Why = base::Format("{}: the envelope did not come back", GetTargetProfile(Target).m_pId);
					break;
				}
				const CEnvelopePoint &Point = Back.m_vEnvelopes[0].m_vPoints[1];
				if(Point.m_Curvetype != CURVETYPE_BEZIER)
				{
					bOk = false;
					Why = base::Format("{}: the curve type is {:d}, not bezier", GetTargetProfile(Target).m_pId, Point.m_Curvetype);
					break;
				}
				for(int c = 0; c < 4; c++)
					if(Point.m_Bezier.m_aInTangentdx[c] != B.m_Bezier.m_aInTangentdx[c] ||
						Point.m_Bezier.m_aInTangentdy[c] != B.m_Bezier.m_aInTangentdy[c] ||
						Point.m_Bezier.m_aOutTangentdx[c] != B.m_Bezier.m_aOutTangentdx[c] ||
						Point.m_Bezier.m_aOutTangentdy[c] != B.m_Bezier.m_aOutTangentdy[c])
					{
						bOk = false;
						Why = base::Format("{}: channel {:d} lost its tangents", GetTargetProfile(Target).m_pId, c);
						break;
					}
				if(!bOk)
					break;
			}

			if(bOk)
				std::cout << "  ok   curve  a custom bezier curve survives every target, Teeworlds style\n";
			else
			{
				std::cout << "  FAIL curve  " << Why << "\n";
				Failures++;
			}
		}

		// ------------------------------------------------- quad corner colours
		/*
			A quad has four corner colours, and the picker aims at one of them.
			Colouring "the quad" used to write all four at once, which made a
			gradient impossible to build; this pins the two behaviours the
			editor offers: one corner, or all four together.
		*/
		{
			Checks++;
			CMap Map;
			Map.InitNew("corner-colour");
			CGroup &Group = Map.m_vGroups.back();
			CLayer &Layer = Map.NewQuadLayer(Group, -1);
			Layer.m_vQuads.emplace_back();
			CQuad &Quad = Layer.m_vQuads.back();
			for(int i = 0; i < 4; i++)
			{
				Quad.m_aColors[i].r = 10;
				Quad.m_aColors[i].g = 20;
				Quad.m_aColors[i].b = 30;
				Quad.m_aColors[i].a = 40;
			}

			// one corner: only that one changes
			const CColor One{200, 100, 50, 255};
			Quad.m_aColors[1] = One;
			bool bOk = Quad.m_aColors[1].r == One.r && Quad.m_aColors[0].r == 10 && Quad.m_aColors[2].r == 10 && Quad.m_aColors[3].r == 10;

			// all four: every corner takes the colour, which is what the
			// "all corners" target and the layer panel swatch do
			const CColor All{7, 8, 9, 10};
			for(int i = 0; i < 4; i++)
				Quad.m_aColors[i] = All;
			for(int i = 0; i < 4; i++)
				bOk = bOk && Quad.m_aColors[i].r == All.r && Quad.m_aColors[i].g == All.g &&
					Quad.m_aColors[i].b == All.b && Quad.m_aColors[i].a == All.a;

			// and both survive an export, which is what makes them real edits
			for(const map_target Target : AllTargets())
			{
				byte_vector Bytes;
				CMap Back;
				std::string Error;
				if(!Map.SaveToBytes(Bytes, Target, &Error) || !Back.LoadFromBytes(Bytes, &Error))
				{
					bOk = false;
					break;
				}
				bool bFound = false;
				for(const CGroup &BackGroup : Back.m_vGroups)
					for(const CLayer &BackLayer : BackGroup.m_vLayers)
						for(const CQuad &BackQuad : BackLayer.m_vQuads)
							for(const CColor &Corner : BackQuad.m_aColors)
								if(Corner.r == All.r && Corner.g == All.g && Corner.b == All.b && Corner.a == All.a)
									bFound = true;
				if(!bFound)
				{
					bOk = false;
					break;
				}
			}

			if(bOk)
				std::cout << "  ok   quad   a corner colour is set on its own and survives an export\n";
			else
			{
				std::cout << "  FAIL quad   a corner colour is not independent of the other corners\n";
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

		/*
			The idle engine of the windowed editor: the model layer, the agents
			and the screenshot mode all drive this same object. The checks here
			are the ones that do not need a map file, so a regression in a tool
			is caught by `--selftest` rather than by looking at a screenshot.
		*/
		{
			Checks++;
			CEditor Editor;
			Editor.InitHeadless();
			bool bOk = true;

			// the toolbox has to know which tools the canvas can use
			bOk = bOk && Editor.Mode() == edit_mode::tiles;

			// the drag order of a dock is state, so reordering it must stick
			std::vector<CEditor::dock_panel> Order = Editor.Tabs(dock_slot::right_upper);
			bOk = bOk && Order.size() >= 2 && Order[0] == CEditor::dock_panel::tiles;
			std::swap(Order[0], Order[1]);
			Editor.Tabs(dock_slot::right_upper) = Order;
			bOk = bOk && Editor.Tabs(dock_slot::right_upper)[1] == CEditor::dock_panel::tiles;
			// the layer tree is the lower docker, which is where Krita keeps its
			// layer stack, and nothing else lives there by default
			const std::vector<CEditor::dock_panel> &Lower = Editor.Tabs(dock_slot::right_lower);
			bOk = bOk && Lower.size() == 1 && Lower[0] == CEditor::dock_panel::layers;
			bOk = bOk && Editor.DockPanel(dock_slot::right_lower) == CEditor::dock_panel::layers;

			/*
				The panel buttons of the top row work the same state the tabs
				do: a workspace that is showing folds its docker away, one that
				is hidden comes back to the docker it belongs to. It is checked
				here because it is state, not drawing.
			*/
			bOk = bOk && Editor.PanelShown(CEditor::dock_panel::layers) &&
				Editor.PanelShown(CEditor::dock_panel::tiles) && !Editor.PanelShown(CEditor::dock_panel::rules);
			Editor.TogglePanel(CEditor::dock_panel::layers);
			bOk = bOk && !Editor.PanelShown(CEditor::dock_panel::layers) &&
				Editor.DockPanel(dock_slot::right_lower) == CEditor::dock_panel::layers;
			Editor.TogglePanel(CEditor::dock_panel::layers);
			bOk = bOk && Editor.PanelShown(CEditor::dock_panel::layers);
			Editor.TogglePanel(CEditor::dock_panel::rules);
			bOk = bOk && Editor.PanelShown(CEditor::dock_panel::rules) &&
				Editor.DockPanel(dock_slot::right_upper) == CEditor::dock_panel::rules &&
				!Editor.PanelShown(CEditor::dock_panel::tiles);
			// the one that is showing folds away where it is, rather than
			// being moved back to the docker it started in
			Editor.TogglePanel(CEditor::dock_panel::rules);
			bOk = bOk && !Editor.PanelShown(CEditor::dock_panel::rules) &&
				Editor.DockPanel(dock_slot::right_upper) == CEditor::dock_panel::rules;
			bOk = bOk && CEditor::HomeSlot(CEditor::dock_panel::layers) == dock_slot::right_lower &&
				CEditor::HomeSlot(CEditor::dock_panel::rules) == dock_slot::right_upper;
			// and the tool strip can be folded away with them
			bOk = bOk && Editor.ToolBoxShown();
			Editor.ToggleToolBox();
			bOk = bOk && !Editor.ToolBoxShown();
			Editor.ToggleToolBox();

			/*
				The menu of the title bar row. Opening one is what stops the
				canvas from answering the mouse, so what matters here is that
				the state is the editor's and not the drawing code's: the
				offscreen mode opens a menu through exactly this.
			*/
			bOk = bOk && Editor.Menu() == CEditor::menu_id::none;
			Editor.OpenMenu(CEditor::menu_id::files);
			bOk = bOk && Editor.Menu() == CEditor::menu_id::files;
			Editor.OpenMenu(CEditor::menu_id::none);

			/*
				Renaming a layer from its row in the tree. The state machine is
				the editor's rather than the drawing code's, so this runs with
				no window: one frame is the queued text or key, the read, and
				the end of the frame, exactly as the window does it.
			*/
			if(!Editor.Map().m_vGroups.empty() && !Editor.Map().m_vGroups[0].m_vLayers.empty())
			{
				const auto Frame = [&Editor](const char *pText, int Key) {
					Editor.Input().NewFrame();
					if(pText != nullptr)
						Editor.Input().OnText(pText);
					if(Key != 0)
						Editor.Input().OnKey(Key, engine::CInput::action_press, 0);
					Editor.HandleRenameInput();
					Editor.Input().EndFrame();
				};
				const std::string Original = Editor.Layer(0, 0)->m_Name;
				Editor.BeginRename(0, 0);
				// the name that was there comes up selected, so the first
				// letter typed replaces it instead of being appended
				bOk = bOk && Editor.Renaming() && Editor.RenameText() == Original && Editor.RenameAllSelected();
				Frame("renamed", 0);
				bOk = bOk && Editor.RenameText() == "renamed" && !Editor.RenameAllSelected();
				Frame(nullptr, engine::keys::Return);
				bOk = bOk && !Editor.Renaming() && Editor.Layer(0, 0)->m_Name == "renamed";
				// and it is a step of the undo history like any other edit
				bOk = bOk && Editor.CanUndo();
				Editor.UndoOp();
				bOk = bOk && Editor.Layer(0, 0)->m_Name == Original;
				// Escape leaves the name exactly as it was
				Editor.BeginRename(0, 0);
				Frame("nope", engine::keys::Escape);
				bOk = bOk && !Editor.Renaming() && Editor.Layer(0, 0)->m_Name == Original;
			}

			// the image and sound operations are part of the model, so they run
			// without a renderer: an image that is added is listed, embedding a
			// mapres one is refused when the name does not exist, and removing
			// an image renumbers the layers that referenced it
			std::string Error;
			const int Added = Editor.AddImageRGBA("probe", 2, 2, byte_vector(16, std::byte{0xff}), &Error);
			bOk = bOk && Added == 1 && Editor.ImageCount() == 2 && Error.empty();
			bOk = bOk && Editor.FindImage("probe") == 1;
			const int Bad = Editor.AddImageFromMapres("there_is_no_such_tileset", &Error);
			bOk = bOk && Bad == -1 && !Error.empty();
			bOk = bOk && Editor.RemoveImage(0, &Error) && Editor.ImageCount() == 1 &&
				Editor.FindImage("probe") == 0;
			// a layer that used the removed image has no image rather than a
			// stale index into the list that shifted
			for(const CGroup &Group : Editor.Map().m_vGroups)
				for(const CLayer &Layer : Group.m_vLayers)
					bOk = bOk && Layer.m_Image <= 0;

			// a sound is embedded by definition, and its layer names it
			const int Sound = Editor.AddSoundData("probe", byte_vector(64, std::byte{0}), true, &Error);
			bOk = bOk && Sound == 0 && Editor.SoundCount() == 1 && Editor.SoundReferenceCount(0) == 0;
			Editor.AddSoundLayerOp(0);
			bOk = bOk && Editor.SoundLayerIndex() == 0 && Editor.SoundSourceCount() == 0;
			bOk = bOk && Editor.AddSoundSource(100.0f, 200.0f) == 0 && Editor.SoundSourceCount() == 1;
			bOk = bOk && Editor.SoundReferenceCount(0) == 1;
			bOk = bOk && Editor.RemoveSound(0, &Error) && Editor.SoundCount() == 0;

			if(bOk)
				std::cout << "  ok   editor  the toolbox, the dock order and the asset model work without a window\n";
			else
			{
				std::cout << "  FAIL editor  a model operation did not behave\n";
				Failures++;
			}
		}

		/*
			Colouring a quad corner from the map: a right click on a point is
			what aims the picker, so the corner a click means is the corner
			under the cursor, and nothing in the panel has to be set first. The
			check drives the real path - a headless frame with the mouse on the
			corner - because the dispatch is part of the behaviour: on a quad
			layer the right button must not also reach for the tile brush.
		*/
		{
			Checks++;
			CEditor Editor;
			Editor.InitHeadless();
			bool bOk = true;

			CGroup &Group = Editor.Map().m_vGroups[0];
			CLayer &QuadLayer = Editor.Map().NewQuadLayer(Group, -1);
			QuadLayer.m_vQuads.emplace_back();
			CQuad &Quad = QuadLayer.m_vQuads.back();
			// a 100 by 100 quad stored top left / top right / bottom left /
			// bottom right, with its pivot in the middle
			const int aCorners[4][2] = {{0, 0}, {100, 0}, {0, 100}, {100, 100}};
			for(int i = 0; i < 4; i++)
			{
				Quad.m_aPoints[i].x = aCorners[i][0] * 1024;
				Quad.m_aPoints[i].y = aCorners[i][1] * 1024;
			}
			Quad.m_aPoints[4].x = 50 * 1024;
			Quad.m_aPoints[4].y = 50 * 1024;
			/*
				A second quad in the far corner of the map, so the check below
				has a point that really is near the corner of the canvas rather
				than one it assumes is.
			*/
			QuadLayer.m_vQuads.emplace_back();
			CQuad &FarQuad = QuadLayer.m_vQuads.back();
			const int aFar[4][2] = {{1500, 1500}, {1600, 1500}, {1500, 1600}, {1600, 1600}};
			for(int i = 0; i < 4; i++)
			{
				FarQuad.m_aPoints[i].x = aFar[i][0] * 1024;
				FarQuad.m_aPoints[i].y = aFar[i][1] * 1024;
			}
			FarQuad.m_aPoints[4].x = 1550 * 1024;
			FarQuad.m_aPoints[4].y = 1550 * 1024;
			Editor.SetSelectedLayer(0, static_cast<int>(Group.m_vLayers.size()) - 1);
			bOk = bOk && Editor.Mode() == edit_mode::quads;
			Editor.SelectQuad(0);

			// one frame lays the canvas out, the way the window does before it
			// hit tests anything
			Editor.UpdateHeadless();

			// the pivot carries no colour, so it neither opens the picker nor
			// aims it at a corner that was not clicked
			bOk = bOk && !Editor.OpenQuadColorPickerAt(Editor.QuadPointScreen(0, 4), -1);
			bOk = bOk && !Editor.ColorPopupShown() && Editor.QuadColorTarget() == CEditor::AllQuadCorners;

			// a well clear point: no handle is within grabbing distance of it,
			// whichever way the view happens to be laid out
			const base::vec2 Far = Editor.QuadPointScreen(0, 4) + base::vec2(300.0f, 300.0f);

			// a right click that hits no quad at all leaves everything alone
			bOk = bOk && !Editor.OpenQuadColorPickerAt(Far, -1);
			bOk = bOk && !Editor.ColorPopupShown();

			// and a corner, through a real frame: the mouse is put on the top
			// right corner and the right button pressed there
			const base::vec2 Corner = Editor.QuadPointScreen(0, 1);
			Editor.Input().OnCursorPos(Corner.x, Corner.y);
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::right), engine::CInput::action_press);
			Editor.UpdateHeadless();
			bOk = bOk && Editor.ColorPopupShown() && Editor.QuadColorTarget() == 1;
			bOk = bOk && Editor.SelectedQuads().size() == 1 && Editor.SelectedQuads()[0] == 0;
			// the tile brush is not what the right button means on a quad layer
			bOk = bOk && Editor.Status().find("tile layer") == std::string::npos;
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::right), engine::CInput::action_release);
			Editor.UpdateHeadless();
			bOk = bOk && Editor.ColorPopupShown();

			// a point inside the shape, away from the handles, is the quad
			// rather than one of its corners: all four of them at once
			bOk = bOk && Editor.OpenQuadColorPickerAt(Far, 0);
			bOk = bOk && Editor.QuadColorTarget() == CEditor::AllQuadCorners;

			/*
				The popup sits beside the point it was opened from rather than
				over it - the corner being coloured has to stay visible - and a
				point that is itself in a corner of the canvas flips it to the
				other side instead of pushing it off the window.
			*/
			const auto Near = [](const base::rect &Rect, base::vec2 Point) {
				const float dx = std::max(std::max(Rect.m_TopLeft.x - Point.x, Point.x - Rect.m_BottomRight.x), 0.0f);
				const float dy = std::max(std::max(Rect.m_TopLeft.y - Point.y, Point.y - Rect.m_BottomRight.y), 0.0f);
				return std::sqrt(dx * dx + dy * dy);
			};
			const auto InsideWindow = [](const base::rect &Rect, base::vec2 Window) {
				return Rect.m_TopLeft.x >= 0.0f && Rect.m_TopLeft.y >= 0.0f &&
					Rect.m_BottomRight.x <= Window.x && Rect.m_BottomRight.y <= Window.y;
			};
			const base::vec2 Window = Editor.WindowSize();
			Editor.OpenQuadColorPickerAt(Corner, -1);
			const base::rect Beside = Editor.ColorPopupRect();
			bOk = bOk && Editor.ColorPopupShown() && !Beside.Contains(Corner);
			// down and to the right of the point, close enough to belong to it
			bOk = bOk && Beside.m_TopLeft.x > Corner.x && Beside.m_TopLeft.y > Corner.y;
			bOk = bOk && Near(Beside, Corner) <= 20.0f && InsideWindow(Beside, Window);

			Editor.SelectQuad(1);
			const base::vec2 BottomRight = Editor.QuadPointScreen(1, 3);
			bOk = bOk && Editor.OpenQuadColorPickerAt(BottomRight, -1);
			const base::rect Flipped = Editor.ColorPopupRect();
			// the point is in the corner of the canvas, so the popup is the
			// other way round: above and to the left of it
			bOk = bOk && !Flipped.Contains(BottomRight);
			bOk = bOk && Flipped.m_BottomRight.x < BottomRight.x && Flipped.m_BottomRight.y < BottomRight.y;
			bOk = bOk && Near(Flipped, BottomRight) <= 60.0f && InsideWindow(Flipped, Window);
			Editor.SelectQuad(0);

			/*
				A click that is not on the popup closes it, and that frame does
				nothing else with the click: the picker is a popup, so clicking
				away is the way out and Escape is only the other one. The closing
				click here is a right click on the same corner, which would open
				the picker again if the canvas had seen it.
			*/
			Editor.OpenQuadColorPickerAt(Corner, -1);
			bOk = bOk && Editor.ColorPopupShown();
			const base::vec2 Inside = Editor.ColorPopupRect().Center();
			Editor.Input().OnCursorPos(Inside.x, Inside.y);
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::left), engine::CInput::action_press);
			Editor.UpdateHeadless();
			bOk = bOk && Editor.ColorPopupShown();
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::left), engine::CInput::action_release);
			Editor.UpdateHeadless();
			Editor.Input().OnCursorPos(Corner.x, Corner.y);
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::right), engine::CInput::action_press);
			Editor.UpdateHeadless();
			bOk = bOk && !Editor.ColorPopupShown();
			Editor.Input().OnMouseButton(static_cast<int>(engine::CInput::mouse_button::right), engine::CInput::action_release);
			Editor.UpdateHeadless();

			// a point of a quad is reported where the transform puts it, so a
			// script can aim at a corner instead of at a pixel
			const base::vec2 Left = Editor.QuadPointScreen(0, 0);
			bOk = bOk && Corner.x > Left.x && std::abs(Corner.y - Left.y) < 0.5f;
			bOk = bOk && Editor.QuadPointScreen(2, 0).x < 0.0f && Editor.QuadPointScreen(0, 5).x < 0.0f;

			if(bOk)
				std::cout << "  ok   quad   a right click colours the corner it lands on\n";
			else
			{
				std::cout << "  FAIL quad   a right click does not aim the picker at the corner under it\n";
				Failures++;
			}
		}

		/*
			The window furniture of a window with a drawn title bar. Nothing
			here needs a display: the policy is a plain function of the chrome
			the editor publishes, which is the point of keeping it out of the
			window code.
		*/
		{
			Checks++;
			bool bOk = true;
			const base::vec2 Size(1600.0f, 900.0f);

			engine::CClientChrome Chrome;
			Chrome.m_bEnabled = true;
			Chrome.m_HeaderHeight = 40.0f;
			Chrome.m_Edge = 4.0f;
			// a "Save as" button in the bar, and the close button in the corner
			Chrome.m_vControls.push_back(
				base::rect(base::vec2(100.0f, 8.0f), base::vec2(160.0f, 32.0f)));
			Chrome.m_vControls.push_back(
				base::rect(base::vec2(1562.0f, 4.0f), base::vec2(1596.0f, 36.0f)));

			const auto Region = [&](float x, float y) {
				return engine::HitTestChrome(Chrome, Size, base::vec2(x, y));
			};
			// the empty part of the bar is what the window is grabbed by
			bOk = bOk && Region(800.0f, 20.0f) == engine::window_region::drag;
			// the canvas below it is not furniture
			bOk = bOk && Region(800.0f, 300.0f) == engine::window_region::normal;
			// a button stays a button, wherever it is in the bar
			bOk = bOk && Region(120.0f, 20.0f) == engine::window_region::normal;
			// the four edges, the four corners, and the close button that sits
			// in the top right one: it wins over the corner it is inside
			bOk = bOk && Region(700.0f, 1.0f) == engine::window_region::resize_top;
			bOk = bOk && Region(700.0f, 899.0f) == engine::window_region::resize_bottom;
			bOk = bOk && Region(0.0f, 400.0f) == engine::window_region::resize_left;
			bOk = bOk && Region(1599.0f, 400.0f) == engine::window_region::resize_right;
			bOk = bOk && Region(1.0f, 1.0f) == engine::window_region::resize_top_left;
			bOk = bOk && Region(1599.0f, 1.0f) == engine::window_region::resize_top_right;
			bOk = bOk && Region(1.0f, 899.0f) == engine::window_region::resize_bottom_left;
			bOk = bOk && Region(1599.0f, 899.0f) == engine::window_region::resize_bottom_right;
			// a window that kept its system title bar has no furniture of its
			// own at all
			Chrome.m_bEnabled = false;
			bOk = bOk && Region(800.0f, 20.0f) == engine::window_region::normal;
			bOk = bOk && Region(0.0f, 400.0f) == engine::window_region::normal;

			if(bOk)
				std::cout << "  ok   chrome  the drawn title bar drags, resizes and keeps its buttons\n";
			else
			{
				std::cout << "  FAIL chrome  the window hit test is wrong\n";
				Failures++;
			}
		}

		/*
			The vector icons. The tool strip gets its pictures from SVG
			documents, so the parser and the rasterizer have to work on a
			machine with no window: the same drawing is rasterized at two sizes
			and the covered area has to grow with the square of the size, which
			is what "the texture follows the display" means in practice.
		*/
		{
			Checks++;
			bool bOk = true;
			const storage::path Icons = storage::GetDataDirectory() / "icons";

			// the size policy: the layout's size times the display scale
			bOk = bOk && engine::svg::RasterSize(18.0f, 1.0f) == 18;
			bOk = bOk && engine::svg::RasterSize(18.0f, 2.0f) == 36;
			bOk = bOk && engine::svg::RasterSize(18.0f, 0.0f) == 4;
			bOk = bOk && engine::svg::RasterSize(600.0f, 4.0f) == 512;

			engine::svg::CDocument Hand;
			bOk = bOk && Hand.LoadFromFile(Icons / "pan.svg");
			bOk = bOk && Hand.Loaded() && Hand.Size().x == 24.0f && Hand.Size().y == 24.0f;

			const auto Coverage = [](const byte_vector &Pixels) {
				double Sum = 0.0;
				for(std::size_t i = 3; i < Pixels.size(); i += 4)
					Sum += static_cast<double>(std::to_integer<unsigned char>(Pixels[i]));
				return Sum / 255.0;
			};

			byte_vector Small;
			byte_vector Large;
			bOk = bOk && Hand.Rasterize(24, 24, Small) && Small.size() == 24u * 24u * 4u;
			bOk = bOk && Hand.Rasterize(48, 48, Large) && Large.size() == 48u * 48u * 4u;
			const double SmallCoverage = Coverage(Small);
			const double LargeCoverage = Coverage(Large);
			// four times the pixels, so about four times the covered area
			bOk = bOk && SmallCoverage > 20.0 && LargeCoverage > 3.6 * SmallCoverage &&
				LargeCoverage < 4.4 * SmallCoverage;

			// every icon of the chrome has to parse and to cover something
			for(const char *pName : {"pan", "brush", "bucket", "eraser", "marquee", "frame", "grid", "keyboard",
					"toolbox", "layers", "tiles", "assets", "envelope", "rules"})
			{
				engine::svg::CDocument Icon;
				if(!Icon.LoadFromFile(Icons / (std::string(pName) + ".svg")))
				{
					bOk = false;
					continue;
				}
				byte_vector Pixels;
				if(!Icon.Rasterize(16, 16, Pixels) || Coverage(Pixels) < 1.5)
					bOk = false;
			}

			if(bOk)
				std::cout << base::Format("  ok   svg     the icons parse and rasterize at {:d}x{:d} and {:d}x{:d}"
					" ({:.0f} and {:.0f} covered pixels)\n", 24, 24, 48, 48, SmallCoverage, LargeCoverage);
			else
			{
				std::cout << base::Format("  FAIL svg     an icon did not rasterize ({:.0f} and {:.0f} covered pixels)\n",
					SmallCoverage, LargeCoverage);
				Failures++;
			}
		}

		// ------------------------------------------------------ headless render
		/*
			The renderer is how a map is pictured with no window, so the two
			things a dump cannot show are pinned here: a tile really fills its
			32 pixel cell (drawing it one pixel wide was the first bug), and a
			semi transparent quad is composited once rather than once per
			overlapping triangle, which would show as a seam.
		*/
		{
			Checks++;
			CMap SheetMap;
			SheetMap.InitNew("render-tile");
			CImage Sheet;
			Sheet.m_Name = "test_sheet";
			Sheet.m_External = 0;
			Sheet.m_Width = 16;
			Sheet.m_Height = 16;
			Sheet.m_Data.assign(16 * 16 * 4, std::byte{0});
			for(std::size_t i = 0; i < 16 * 16; i++)
			{
				Sheet.m_Data[i * 4 + 0] = static_cast<std::byte>(255);
				Sheet.m_Data[i * 4 + 3] = static_cast<std::byte>(255);
			}
			// InitNew installs a default external image at index 0; replace it so
			// the layer really points at this embedded sheet and no file is needed
			SheetMap.m_vImages.clear();
			SheetMap.m_vImages.push_back(Sheet);

			// the plain tile layer of the last group, one red tile at (0,0)
			CGroup &TileGroup = SheetMap.m_vGroups.back();
			for(CLayer &Layer : TileGroup.m_vLayers)
				if(Layer.IsTiles())
				{
					Layer.Resize(2, 2);
					Layer.m_Image = 0;
					Layer.m_Color = CColor{255, 255, 255, 255};
					Layer.m_vTiles[0].m_Index = 1;
				}

			CMapRenderOptions Options;
			Options.m_Width = 64;
			Options.m_Height = 64;
			Options.m_bTransparent = true;
			Options.m_bCenterSet = true;
			Options.m_Center = base::vec2(16.0f, 16.0f);
			Options.m_Zoom = 1.0f;

			storage::byte_vector Pixels;
			CMapRenderStats Stats;
			std::string Error;
			bool bOk = RenderMapToRgba(SheetMap, Options, Pixels, Stats, Error);
			int Covered = 0;
			for(std::size_t i = 0; i < static_cast<std::size_t>(Options.m_Width) * Options.m_Height; i++)
				if(Pixels[i * 4 + 3] != std::byte{0})
					Covered++;
			bOk = bOk && Covered == 32 * 32;
			const std::size_t Center = (static_cast<std::size_t>(32) * Options.m_Width + 32) * 4;
			bOk = bOk && Pixels[Center + 0] == std::byte{255} && Pixels[Center + 1] == std::byte{0} &&
				Pixels[Center + 2] == std::byte{0} && Pixels[Center + 3] == std::byte{255};

			if(bOk)
				std::cout << "  ok   render a tile fills its cell and keeps the layer colour\n";
			else
			{
				std::cout << "  FAIL render a tile did not fill its 32x32 cell (" << Covered << " pixels)\n";
				Failures++;
			}
		}

		{
			Checks++;
			CMap QuadMap;
			QuadMap.InitNew("render-quad");
			QuadMap.m_vGroups.clear();
			CGroup &Group = QuadMap.NewGroup();
			CLayer &Layer = QuadMap.NewQuadLayer(Group, -1);
			Layer.m_vQuads.emplace_back();
			CQuad &Quad = Layer.m_vQuads.back();
			// a 64 by 64 world square, all four corners half transparent
			for(int i = 0; i < 4; i++)
			{
				Quad.m_aColors[i] = CColor{255, 255, 255, 128};
				Quad.m_aPoints[i].x = (i % 2) * 64 * 1024;
				Quad.m_aPoints[i].y = (i / 2) * 64 * 1024;
			}
			Quad.m_aPoints[4].x = 32 * 1024;
			Quad.m_aPoints[4].y = 32 * 1024;

			CMapRenderOptions Options;
			Options.m_Width = 64;
			Options.m_Height = 64;
			Options.m_bTransparent = true;
			Options.m_bCenterSet = true;
			Options.m_Center = base::vec2(32.0f, 32.0f);
			Options.m_Zoom = 1.0f;

			storage::byte_vector Pixels;
			CMapRenderStats Stats;
			std::string Error;
			bool bOk = RenderMapToRgba(QuadMap, Options, Pixels, Stats, Error);
			int Covered = 0;
			for(std::size_t i = 0; i < static_cast<std::size_t>(Options.m_Width) * Options.m_Height; i++)
			{
				const unsigned char Alpha = static_cast<unsigned char>(Pixels[i * 4 + 3]);
				if(Alpha == 0)
					continue;
				Covered++;
				if(Alpha != 128)
					bOk = false;
			}
			bOk = bOk && Covered == 64 * 64;

			if(bOk)
				std::cout << "  ok   render a half transparent quad is composited exactly once\n";
			else
			{
				std::cout << "  FAIL render a quad was blended more than once (" << Covered << " pixels)\n";
				Failures++;
			}
		}

		std::cout << base::Format("\n{:d} checks, {:d} failures\n", Checks, Failures);
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
			// --help and -h print the usage, whatever follows them
			if(Args.m_Command == "--help" || Args.m_Command == "-h")
			{
				PrintUsage();
				return 0;
			}
			/*
				No command at all: nothing to do is the editor with no map, and
				a single file is the editor opened on it, which is what the
				usage line promises (`uniteed map.map`). Anything more is a
				command line that was mistyped.
			*/
			if(Args.m_vPositional.size() <= 1)
				return -1;
			PrintUsage();
			return 0;
		}

		if(Args.m_Command == "--gui")
			return -1; // the window, which main() starts

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
				Package.AttachMapResources();
				// the helper skips a mapres it cannot find, which for a person
				// looking at the output is worth saying out loud
				for(const CImage &Image : Map.m_vImages)
					if(!Image.IsEmbedded() && Package.FindResource("mapres/" + Image.m_Name + ".png") == nullptr)
						std::cout << "warning: no mapres file for image '" << Image.m_Name << "'\n";
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

		if(Args.m_Command == "--render")
		{
			if(Args.m_vPositional.size() < 2)
			{
				std::cout << "--render needs an input map and an output image\n";
				return 2;
			}
			CMap Map;
			std::string Error;
			if(!Map.LoadFromFile(Args.m_vPositional[0], &Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}

			CMapRenderOptions Options;
			Options.m_Width = Args.m_bSizeSet ? Args.m_Width : 800;
			Options.m_Height = Args.m_bSizeSet ? Args.m_Height : 600;
			Options.m_bCenterSet = Args.m_bCenterSet;
			Options.m_Center = Args.m_Center;
			Options.m_Zoom = Args.m_Zoom;
			Options.m_Time = Args.m_Time;
			Options.m_bTransparent = Args.m_bTransparent;
			Options.m_Background = Args.m_Background;

			storage::byte_vector Pixels;
			CMapRenderStats Stats;
			if(!RenderMapToRgba(Map, Options, Pixels, Stats, Error))
			{
				std::cout << "error: " << Error << "\n";
				return 1;
			}
			if(!base::WritePng(Args.m_vPositional[1], Options.m_Width, Options.m_Height, Pixels))
			{
				std::cout << "error: could not write '" << Args.m_vPositional[1] << "'\n";
				return 1;
			}
			std::cout << base::Format("rendered {}x{} to {}: {} groups, {} layers, {} tiles, {} quads, {} images{}\n",
				Options.m_Width, Options.m_Height, Args.m_vPositional[1], Stats.m_Groups, Stats.m_Layers,
				Stats.m_Tiles, Stats.m_Quads, Stats.m_Images,
				Stats.m_MissingImages > 0 ? base::Format(", {} missing", Stats.m_MissingImages) : std::string());
			return 0;
		}

		std::cout << "unknown command '" << Args.m_Command << "'\n\n";
		PrintUsage();
		return 2;
	}
} // namespace game
