/*
	The rule engine: the DDRaceNetwork automapper algorithm, with the layer
	addressing of map.h instead of the two fixed layers.

	The reference is ddnet src/game/editor/auto_map.cpp (AutoMap and Proceed).
	Everything that decides *which* tile is written is reproduced exactly,
	including the awkward parts, because a rule set that works in the DDRace
	editor has to produce the same map here:

	  * the read layer of the first run is the game layer when a reference id is
	    selected, and the applied layer otherwise
	  * an empty read tile clears a non empty target tile when that filter is
	    active, and otherwise falls through to m_bSkipEmpty
	  * the random check is HashLocation(Seed, Run, RuleIndex, X, Y) against
	    HASH_MAX * probability, so it is deterministic per position
	  * the flags a rule writes are its own m_Flag, not the source tile's
*/

#include "game/rules/rules.h"

#include <algorithm>
#include <cstdint>

#include "base/logger.h"

namespace game::rules
{
	namespace
	{
		// the reference's HashUInt32 / HashLocation, from DDNet's auto_map.cpp
		constexpr std::uint32_t HashUInt32(std::uint32_t Num)
		{
			Num++;
			Num ^= Num >> 17;
			Num *= 0xed5ad4bbu;
			Num ^= Num >> 11;
			Num *= 0xac4c1b51u;
			Num ^= Num >> 15;
			Num *= 0x31848babu;
			Num ^= Num >> 14;
			return Num;
		}

		constexpr int HashMax = 65536;

		int HashLocation(std::uint32_t Seed, std::uint32_t Run, std::uint32_t Rule, std::uint32_t X, std::uint32_t Y)
		{
			const std::uint32_t Prime = 31;
			std::uint32_t Hash = 1;
			Hash = Hash * Prime + HashUInt32(Seed);
			Hash = Hash * Prime + HashUInt32(Run);
			Hash = Hash * Prime + HashUInt32(Rule);
			Hash = Hash * Prime + HashUInt32(X);
			Hash = Hash * Prime + HashUInt32(Y);
			Hash = HashUInt32(Hash * Prime);
			return static_cast<int>(Hash % HashMax);
		}

		// the flag bits a pos rule compares: the reference masks to the three
		// orientation bits and ignores OPAQUE, which a mapmaker never sets by hand
		constexpr int OrientationFlags = 0x1 | 0x2 | 0x8; // XFLIP | YFLIP | ROTATE

		int InfoTileIndex(const CIndexInfo &Info)
		{
			return Info.m_Role >= 0 ? RoleTileIndex(static_cast<tile_role>(Info.m_Role)) : Info.m_Tile;
		}

		struct CTileBuffer
		{
			std::vector<std::uint8_t> m_Tiles;
			std::vector<std::uint8_t> m_Flags;
			int m_Width = 0;
			int m_Height = 0;

			void Resize(int Width, int Height)
			{
				m_Width = std::max(Width, 0);
				m_Height = std::max(Height, 0);
				m_Tiles.assign(static_cast<std::size_t>(m_Width) * m_Height, 0);
				m_Flags.assign(static_cast<std::size_t>(m_Width) * m_Height, 0);
			}

			CRuleLayer Layer() { return CRuleLayer{m_Tiles.data(), m_Flags.data(), m_Width, m_Height}; }
		};

		/*
			The rectangle a configuration applies to, in tile coordinates and
			with an exclusive end, so an all-zero region is the whole layer. The
			conditions may still read outside it: only the write is clipped,
			which is what lets a rule on the edge of a region look at its
			neighbour and is how the DDRace editor reads the field too.
		*/
		void AutomapRun(CRuleLayer Target, CRuleLayer Read, const CRun &Run, std::size_t RunIndex,
			bool bFilterable, int Seed, int SeedOffsetX, int SeedOffsetY, int &TilesWritten,
			int X0, int Y0, int X1, int Y1)
		{
			const int Width = Target.m_Width;
			const int Height = Target.m_Height;

			for(int y = Y0; y < Y1; y++)
			{
				for(int x = X0; x < X1; x++)
				{
					const int ReadIndex = Read.InBounds(x, y) ? Read.Index(x, y) : 0;

					for(std::size_t i = 0; i < Run.m_vIndexRules.size(); i++)
					{
						const CIndexRule &Rule = Run.m_vIndexRules[i];
						const int RuleTile = Rule.m_Role >= 0 ? RoleTileIndex(static_cast<tile_role>(Rule.m_Role)) : Rule.m_Tile;

						if(ReadIndex == 0)
						{
							/*
								The filterable first run of a rule set that reads
								the game layer: a non empty target tile that has
								no source tile behind it is *cleared*, so the
								automap starts from the game layer's shape.
							*/
							if(Target.Index(x, y) != 0 && bFilterable)
							{
								Target.Set(x, y, 0, Rule.m_Flag);
								TilesWritten++;
								continue;
							}
							if(Rule.m_bSkipEmpty)
								continue;
						}
						if(Rule.m_bSkipFull && ReadIndex != 0)
							continue;

						bool bRespectRules = true;
						for(std::size_t j = 0; j < Rule.m_vRules.size() && bRespectRules; j++)
						{
							const CPosRule &PosRule = Rule.m_vRules[j];

							int CheckIndex = -1;
							int CheckFlags = 0;
							const int CheckX = x + PosRule.m_X;
							const int CheckY = y + PosRule.m_Y;
							if(CheckX >= 0 && CheckX < Width && CheckY >= 0 && CheckY < Height)
							{
								CheckIndex = Read.Index(CheckX, CheckY);
								CheckFlags = Read.Flags(CheckX, CheckY) & OrientationFlags;
							}

							if(PosRule.m_Mode == pos_rule_mode::index)
							{
								bRespectRules = false;
								for(const CIndexInfo &Info : PosRule.m_vIndices)
									if(CheckIndex == InfoTileIndex(Info) && (!Info.m_bTestFlag || CheckFlags == Info.m_Flag))
									{
										bRespectRules = true;
										break;
									}
							}
							else if(PosRule.m_Mode == pos_rule_mode::not_index)
							{
								for(const CIndexInfo &Info : PosRule.m_vIndices)
									if(CheckIndex == InfoTileIndex(Info) && (!Info.m_bTestFlag || CheckFlags == Info.m_Flag))
									{
										bRespectRules = false;
										break;
									}
							}
						}

						bool bPassesModulo = true;
						if(!Rule.m_vModuloRules.empty())
						{
							bPassesModulo = false;
							for(const CModuloRule &Modulo : Rule.m_vModuloRules)
							{
								const int ModX = std::max(Modulo.m_ModX, 1);
								const int ModY = std::max(Modulo.m_ModY, 1);
								if(((x + SeedOffsetX + Modulo.m_OffsetX) % ModX + ModX) % ModX == 0 &&
									((y + SeedOffsetY + Modulo.m_OffsetY) % ModY + ModY) % ModY == 0)
								{
									bPassesModulo = true;
									break;
								}
							}
						}

						if(!bRespectRules || !bPassesModulo)
							continue;
						if(Rule.m_RandomProbability < 1.0f &&
							HashLocation(static_cast<std::uint32_t>(Seed), static_cast<std::uint32_t>(RunIndex),
								static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(x + SeedOffsetX),
								static_cast<std::uint32_t>(y + SeedOffsetY)) >= HashMax * Rule.m_RandomProbability)
							continue;

						Target.Set(x, y, RuleTile, Rule.m_Flag);
						TilesWritten++;
					}
				}
			}
		}
	} // namespace

	// ----------------------------------------------------------------- roles
	namespace
	{
		struct CRoleEntry
		{
			tile_role m_Role;
			const char *m_pName;
			int m_Tile;
		};

		/*
			The tile a role stands for, per the DDRaceNetwork tile list. The
			numbers are stable across every DDRace tileset, which is why a rule
			can name a role instead of an index.

			i18n:table - the editor draws these names through Tr(RoleName(...)).
		*/
		const CRoleEntry g_aRoles[] = {
			{tile_role::game_layer, "game", 0},
			{tile_role::hookable, "hookable", 1},
			{tile_role::death, "death", 2},
			{tile_role::unhookable, "unhookable", 3},
			{tile_role::freeze, "freeze", 9},
			{tile_role::unfreeze, "unfreeze", 11},
			{tile_role::deep_freeze, "deep_freeze", 12},
			{tile_role::deep_unfreeze, "deep_unfreeze", 13},
			{tile_role::live_freeze, "live_freeze", 144},
			{tile_role::live_unfreeze, "live_unfreeze", 145},
		};
	} // namespace

	int RoleTileIndex(tile_role Role)
	{
		for(const CRoleEntry &Entry : g_aRoles)
			if(Entry.m_Role == Role)
				return Entry.m_Tile;
		return 0;
	}

	const char *RoleName(tile_role Role)
	{
		for(const CRoleEntry &Entry : g_aRoles)
			if(Entry.m_Role == Role)
				return Entry.m_pName;
		return "custom";
	}

	tile_role RoleFromName(std::string_view Name)
	{
		for(const CRoleEntry &Entry : g_aRoles)
			if(Name == Entry.m_pName)
				return Entry.m_Role;
		return tile_role::custom;
	}

	const char *AutomapReferenceName(int Reference)
	{
		// the reference list of the DDRace editor, index 0 is the game layer
		// i18n:table - the editor draws these names through Tr(AutomapReferenceName(...))
		static const char *const s_apNames[] = {
			"Game Layer", "Hookable", "Death", "Unhookable", "Freeze", "Unfreeze",
			"Deep Freeze", "Deep Unfreeze", "Live Freeze", "Live Unfreeze"
		};
		if(Reference < 0 || Reference >= NumAutomapReferences)
			return "Unknown";
		return s_apNames[Reference];
	}

	int AutomapReferenceTileIndex(int Reference)
	{
		// 1..9 are the roles, which are exactly entries 1..9 of g_aRoles
		if(Reference <= 0 || Reference >= NumAutomapReferences)
			return 0;
		return g_aRoles[Reference].m_Tile;
	}

	// ------------------------------------------------------------- CRuleLayer
	int CRuleLayer::Index(int X, int Y) const
	{
		if(!InBounds(X, Y) || m_pTiles == nullptr)
			return 0;
		return m_pTiles[static_cast<std::size_t>(Y) * m_Width + X];
	}

	int CRuleLayer::Flags(int X, int Y) const
	{
		if(!InBounds(X, Y) || m_pFlags == nullptr)
			return 0;
		return m_pFlags[static_cast<std::size_t>(Y) * m_Width + X];
	}

	void CRuleLayer::Set(int X, int Y, int Index, int Flags)
	{
		if(!InBounds(X, Y) || m_pTiles == nullptr)
			return;
		const std::size_t At = static_cast<std::size_t>(Y) * m_Width + X;
		m_pTiles[At] = static_cast<std::uint8_t>(std::clamp(Index, 0, 255));
		if(m_pFlags != nullptr)
			m_pFlags[At] = static_cast<std::uint8_t>(std::clamp(Flags, 0, 255));
	}

	int CRuleSet::RuleCount() const
	{
		int Count = 0;
		for(const CConfiguration &Config : m_vConfigs)
			for(const CRun &Run : Config.m_vRuns)
				Count += static_cast<int>(Run.m_vIndexRules.size());
		return Count;
	}

	// ----------------------------------------------------------------- engine
	CApplyResult Apply(const CRuleSet &Rules, int ConfigIndex, CRuleLayer Target,
		const std::vector<CRuleLayer> &ReadLayers, int Seed, int SeedOffsetX, int SeedOffsetY)
	{
		CApplyResult Result;
		if(Target.Empty())
		{
			Result.m_Error = "the target layer is empty";
			return Result;
		}
		if(ConfigIndex < 0 || ConfigIndex >= static_cast<int>(Rules.m_vConfigs.size()))
		{
			Result.m_Error = "no such configuration";
			return Result;
		}
		if(Target.m_pFlags == nullptr)
		{
			Result.m_Error = "the target layer has no flag plane";
			return Result;
		}

		const CConfiguration &Config = Rules.m_vConfigs[static_cast<std::size_t>(ConfigIndex)];
		if(Seed == 0)
			Seed = RandomSeed();

		/*
			The rectangle the configuration applies to, clamped to the target.
			An all-zero width and height means the whole layer, which is what
			every rule set written before the region field existed expects.
		*/
		int X0 = 0, Y0 = 0, X1 = Target.m_Width, Y1 = Target.m_Height;
		if(Config.HasRegion())
		{
			X0 = std::max(0, std::min(Config.m_StartX, Target.m_Width));
			Y0 = std::max(0, std::min(Config.m_StartY, Target.m_Height));
			X1 = std::max(0, std::min(Config.m_EndX, Target.m_Width));
			Y1 = std::max(0, std::min(Config.m_EndY, Target.m_Height));
			if(X1 < X0)
				std::swap(X1, X0);
			if(Y1 < Y0)
				std::swap(Y1, Y0);
		}

		const auto ReadSlot = [&ReadLayers](int Slot) -> CRuleLayer {
			if(Slot < 0 || Slot >= static_cast<int>(ReadLayers.size()))
				return CRuleLayer{};
			return ReadLayers[static_cast<std::size_t>(Slot)];
		};

		/*
			The reference copies the source layer for every run that asks for it
			and then writes into the applied layer. The copy is what makes two
			runs independent: the second run must not see what the first wrote.
		*/
		for(std::size_t RunIndex = 0; RunIndex < Config.m_vRuns.size(); RunIndex++)
		{
			const CRun &Run = Config.m_vRuns[RunIndex];
			const bool bFilterable = RunIndex == 0 && Run.m_ReadSlot >= 0;
			CRuleLayer Read = bFilterable ? ReadSlot(Run.m_ReadSlot) : Target;

			if(Run.m_bCopy)
			{
				CTileBuffer Buffer;
				Buffer.Resize(Target.m_Width, Target.m_Height);
				const int LoopWidth = bFilterable ? std::min(Read.m_Width, Target.m_Width) : Target.m_Width;
				const int LoopHeight = bFilterable ? std::min(Read.m_Height, Target.m_Height) : Target.m_Height;
				// the automap reference: on the first run the snapshot may keep
				// only the tiles of one role, everything else reads as empty
				const int ReferenceTile = bFilterable ? AutomapReferenceTileIndex(Run.m_Reference) : 0;
				for(int y = 0; y < LoopHeight; y++)
					for(int x = 0; x < LoopWidth; x++)
					{
						const int Index = Read.Index(x, y);
						const bool bKeep = Run.m_Reference < 1 || Index == ReferenceTile;
						Buffer.m_Tiles[static_cast<std::size_t>(y) * Buffer.m_Width + x] =
							static_cast<std::uint8_t>(bKeep ? Index : 0);
						Buffer.m_Flags[static_cast<std::size_t>(y) * Buffer.m_Width + x] =
							static_cast<std::uint8_t>(bKeep ? Read.Flags(x, y) : 0);
					}
				Read = Buffer.Layer();
				AutomapRun(Target, Read, Run, RunIndex, bFilterable, Seed, SeedOffsetX, SeedOffsetY, Result.m_TilesWritten,
					X0, Y0, X1, Y1);
			}
			else
			{
				AutomapRun(Target, Read, Run, RunIndex, bFilterable, Seed, SeedOffsetX, SeedOffsetY, Result.m_TilesWritten,
					X0, Y0, X1, Y1);
			}
			Result.m_RunsApplied++;
		}

		Result.m_Seed = Seed;
		Result.m_bOk = true;
		return Result;
	}

	CApplyResult ApplyAll(const CRuleSet &Rules, CRuleLayer Target,
		const std::vector<CRuleLayer> &ReadLayers, int Seed)
	{
		CApplyResult Total;
		// one seed for the whole set, chosen here so every configuration sees
		// the same run and the result can report it
		if(Seed == 0)
			Seed = RandomSeed();
		for(int i = 0; i < static_cast<int>(Rules.m_vConfigs.size()); i++)
		{
			const CApplyResult One = Apply(Rules, i, Target, ReadLayers, Seed);
			if(!One.m_bOk)
			{
				Total.m_Error = One.m_Error;
				return Total;
			}
			Total.m_TilesWritten += One.m_TilesWritten;
			Total.m_RunsApplied += One.m_RunsApplied;
		}
		Total.m_Seed = Seed;
		Total.m_bOk = true;
		return Total;
	}

	int RandomSeed()
	{
		static std::uint32_t s_State = 0x9e3779b9u;
		// a small xorshift, so the seed is reproducible from a log but never 0
		s_State ^= s_State << 13;
		s_State ^= s_State >> 17;
		s_State ^= s_State << 5;
		return static_cast<int>(s_State & 0x7fffffff) + 1;
	}
} // namespace game::rules
