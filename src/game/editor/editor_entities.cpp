/*
	The game layer's entity sprite sheet.

	A game layer cell is not a tile index: it is an offset into the entity
	sheet, with ENTITY_OFFSET added so an empty cell is still 0. The sheet is a
	16 by 16 grid of 64 pixel cells, and the reference client copies cell
	`index % 16`, `index / 16` out of it, so an index the game does not define
	renders as whatever art sits there.

	The DDRace entities only exist in the DDRace sheet, so the sheet follows the
	same switch that gates the entities themselves: a vanilla map is drawn with
	the vanilla sheet, a DDRace one with the DDRace sheet, and each is cached
	under its own slot so switching the switch does not thrash the cache.
*/

#include <string>

#include "base/logger.h"
#include "engine/graphics.h"
#include "game/assets.h"
#include "game/editor/editor.h"

namespace game
{
	/*
		The names of the entities, in one place: the agent interface resolves
		them ("game.tele" and friends take a name or an index) and the palette
		shows them beside the sprite they belong to, so a map maker can tell
		what a cell of the entity sheet is without a manual.
	*/
	namespace
	{
		const CEntityName g_aEntityNames[] = {
			{ENTITY_SPAWN, "spawn"}, {ENTITY_SPAWN_RED, "spawn_red"}, {ENTITY_SPAWN_BLUE, "spawn_blue"},
			{ENTITY_FLAGSTAND_RED, "flag_red"}, {ENTITY_FLAGSTAND_BLUE, "flag_blue"},
			{ENTITY_ARMOR_1, "armor"}, {ENTITY_HEALTH_1, "health"},
			{ENTITY_WEAPON_SHOTGUN, "shotgun"}, {ENTITY_WEAPON_GRENADE, "grenade"},
			{ENTITY_POWERUP_NINJA, "ninja"}, {ENTITY_WEAPON_LASER, "laser"},
			{ENTITY_LASER_FAST_CCW, "laser_fast_ccw"}, {ENTITY_LASER_NORMAL_CCW, "laser_normal_ccw"},
			{ENTITY_LASER_SLOW_CCW, "laser_slow_ccw"}, {ENTITY_LASER_STOP, "laser_stop"},
			{ENTITY_LASER_SLOW_CW, "laser_slow_cw"}, {ENTITY_LASER_NORMAL_CW, "laser_normal_cw"},
			{ENTITY_LASER_FAST_CW, "laser_fast_cw"}, {ENTITY_LASER_SHORT, "laser_short"},
			{ENTITY_LASER_MEDIUM, "laser_medium"}, {ENTITY_LASER_LONG, "laser_long"},
			{ENTITY_LASER_C_SLOW, "laser_c_slow"}, {ENTITY_LASER_C_NORMAL, "laser_c_normal"},
			{ENTITY_LASER_C_FAST, "laser_c_fast"}, {ENTITY_LASER_O_SLOW, "laser_o_slow"},
			{ENTITY_LASER_O_NORMAL, "laser_o_normal"}, {ENTITY_LASER_O_FAST, "laser_o_fast"},
			{ENTITY_PLASMAE, "plasma_e"}, {ENTITY_PLASMAF, "plasma_f"}, {ENTITY_PLASMA, "plasma"},
			{ENTITY_PLASMAU, "plasma_u"}, {ENTITY_CRAZY_SHOTGUN_EX, "crazy_shotgun_ex"},
			{ENTITY_CRAZY_SHOTGUN, "crazy_shotgun"},
			{ENTITY_ARMOR_SHOTGUN, "armor_shotgun"}, {ENTITY_ARMOR_GRENADE, "armor_grenade"},
			{ENTITY_ARMOR_NINJA, "armor_ninja"}, {ENTITY_ARMOR_LASER, "armor_laser"},
			{ENTITY_DRAGGER_WEAK, "dragger_weak"}, {ENTITY_DRAGGER_NORMAL, "dragger"},
			{ENTITY_DRAGGER_STRONG, "dragger_strong"}, {ENTITY_DRAGGER_WEAK_NW, "dragger_weak_nw"},
			{ENTITY_DRAGGER_NORMAL_NW, "dragger_nw"}, {ENTITY_DRAGGER_STRONG_NW, "dragger_strong_nw"},
			{ENTITY_DOOR, "door"},
		};

		std::vector<CEntityName> g_vEntityNames(std::begin(g_aEntityNames), std::end(g_aEntityNames));
	} // namespace

	const std::vector<CEntityName> &EntityNames() { return g_vEntityNames; }

	int EntityFromName(const std::string &Name)
	{
		for(const CEntityName &Entry : g_vEntityNames)
			if(Name == Entry.m_pName)
				return Entry.m_Index;
		return -1;
	}

	const char *EntityName(int Entity)
	{
		for(const CEntityName &Entry : g_vEntityNames)
			if(Entity == Entry.m_Index)
				return Entry.m_pName;
		return nullptr;
	}
} // namespace game

#if UNITEED_GUI

namespace game
{
	namespace
	{
		const char *SheetPath(int Slot)
		{
			// the resource root already is the editor directory, so the entity
			// sheets and the physics sheets are addressed relative to it
			return Slot == 1 ? "entities/DDNet.png" : "entities/Vanilla.png";
		}
	} // namespace

	int CEditor::EntityTexture()
	{
		const int Slot = (m_bAllowExtensions || GetTargetProfile(m_Target).HasExtension(ext_game_entities)) ? 1 : 0;
		if(m_EntityTexture[Slot] != -1)
			return m_EntityTexture[Slot];

		/*
			The other sheet is the fallback, so a build that ships only one of
			them still draws entities; if neither exists the caller gets -1 and
			falls back to the coloured box it drew before.
		*/
		for(const int Candidate : {Slot, 1 - Slot})
		{
			if(m_EntityTexture[Candidate] != -1)
			{
				m_EntityTexture[Slot] = m_EntityTexture[Candidate];
				return m_EntityTexture[Slot];
			}
			const std::optional<storage::path> File = storage::FindResource(SheetPath(Candidate));
			if(!File.has_value())
				continue;
			m_EntityTexture[Candidate] = m_pGraphics->LoadTextureFile(
				std::string("editor:entities:") + std::to_string(Candidate), *File);
			if(m_EntityTexture[Candidate] != -1)
			{
				m_EntityTexture[Slot] = m_EntityTexture[Candidate];
				return m_EntityTexture[Slot];
			}
		}
		LOG_WARN("editor", "no entity sheet under entities/, game entities draw as boxes");
		return -1;
	}

	int CEditor::PhysicsTexture(const CLayer &Layer)
	{
		/*
			One sheet per physics layer, the way the reference data directory
			lays them out. The index is the layer kind, so a map with a tele and
			a speedup layer holds two textures and not one per layer instance.
		*/
		int Index = -1;
		const char *pPath = nullptr;
		if(Layer.m_Flags & TILESLAYERFLAG_FRONT) { Index = 0; pPath = "front.png"; }
		else if(Layer.m_Flags & TILESLAYERFLAG_TELE) { Index = 1; pPath = "tele.png"; }
		else if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP) { Index = 2; pPath = "speedup.png"; }
		else if(Layer.m_Flags & TILESLAYERFLAG_SWITCH) { Index = 3; pPath = "switch.png"; }
		else if(Layer.m_Flags & TILESLAYERFLAG_TUNE) { Index = 4; pPath = "tune.png"; }
		if(Index < 0)
			return -1;
		if(m_PhysicsTexture[Index] != -1)
			return m_PhysicsTexture[Index];

		const std::optional<storage::path> File = storage::FindResource(pPath);
		if(!File.has_value())
			return -1;
		m_PhysicsTexture[Index] = m_pGraphics->LoadTextureFile(std::string("editor:physics:") + std::to_string(Index), *File);
		return m_PhysicsTexture[Index];
	}
} // namespace game

#else

namespace game
{
	int CEditor::EntityTexture() { return -1; }
	int CEditor::PhysicsTexture(const CLayer &) { return -1; }
} // namespace game

#endif
