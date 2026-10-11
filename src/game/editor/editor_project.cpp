/*
	The editor's project, rule, physics layer and envelope operations: the part
	of the model that is shared by the window, the command line and the agent
	interface.

	It lives beside editor.cpp rather than inside it because these are whole
	workspaces rather than canvas behaviour: keeping them here means the file
	that handles the mouse stays about the mouse.
*/

#include <algorithm>
#include <cstring>

#include "base/i18n.h"
#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "game/assets.h"
#include "game/editor/editor.h"
#include "game/map/envelope.h"

namespace game
{
	using base::i18n::Tr;
	// ------------------------------------------------------------------ project
	void CEditor::SetProjectName(const std::string &Name)
	{
		const std::string Clean = storage::SanitiseName(Name);
		m_Project.m_Meta.m_Name = Clean;
		m_Map.m_aVersion = Clean;
	}

	bool CEditor::LoadProject(const std::string &Path)
	{
		CProject Project;
		std::string Error;
		if(!Project.LoadFromFile(Path, &Error))
		{
			SetStatus(base::FormatRuntime(Tr("cannot open the project: {}"), Error));
			return false;
		}

		m_Project = std::move(Project);
		m_Map = m_Project.m_Map;
		m_SourcePath = Path;
		m_bSessionSeeded = true;
		// opening anything means the welcome page has done its job
		m_bShowWelcome = false;
		// a project carries its own record of the mapres version; the map inside
		// a package is always datafile 5, so its layers cannot speak for it
		m_bMapVersionKnown = false;
		m_bPackageLoaded = false;
		m_Package.Clear();
		m_Package.Adopt(m_Map, m_Project.m_Meta.m_Name, Path);
		// a project carries its own mapres files; the editor draws from them
		m_Package.m_vResources = m_Project.m_vResources;

		// the target and the mapres version travel with the project
		if(const std::optional<map_target> Target = TargetFromId(m_Project.m_Meta.m_Target))
			m_Target = *Target;
		if(!m_Project.m_Meta.m_Assets.empty())
		{
			m_TargetVersionId = m_Project.m_Meta.m_Assets;
			// what the project recorded is what its mapres paths point at; the
			// map's own datafile version is not a verdict about the assets,
			// because a package always stores datafile 5
			m_VersionId = m_TargetVersionId;
		}

		// the rules are a document of their own inside the container
		if(m_Project.m_Rules.is_object() && m_Project.m_Rules["autorules"].as_int(0) > 0)
		{
			std::string RuleError;
			m_RuleSet = rules::FromJson(m_Project.m_Rules, &RuleError);
			if(!RuleError.empty())
			{
				LOG_WARN("editor", "the project's rules did not load: {}", RuleError.c_str());
				m_RuleSet = rules::CRuleSet{};
			}
		}
		else
		{
			m_RuleSet = rules::CRuleSet{};
		}

		m_Dirty = false;
		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedGroup = 0;
		m_SelectedLayer = 0;
		m_vSelectedQuads.clear();
		m_SelectedEnvelope = -1;
		m_SelectedEnvelopePoint = -1;
		m_RuleConfig = 0;
		m_RuleRun = 0;
		m_RuleIndex = 0;
		// the project's own record of the mapres version stays authoritative:
		// only a map that arrives without one lets the map decide
		SetupVersionForMap(m_TargetVersionId.empty());
		ResetTextures();
		m_vUndo.clear();
		m_vRedo.clear();

		// the project remembers the camera, so a reopened project looks the same
		if(m_Project.m_Editor.is_object())
		{
			const base::json &Json = m_Project.m_Editor;
			m_View.m_Zoom = static_cast<float>(Json["zoom"].as_double(m_View.m_Zoom));
			m_View.m_Center = base::vec2(static_cast<float>(Json["center_x"].as_double(m_View.m_Center.x)),
				static_cast<float>(Json["center_y"].as_double(m_View.m_Center.y)));
			m_bGrid = Json["grid"].as_bool(m_bGrid);
			m_bShowDetail = Json["show_detail"].as_bool(m_bShowDetail);
			m_bAllowExtensions = Json["allow_extensions"].as_bool(m_bAllowExtensions);
			m_Map.m_bAllowExtensions = m_bAllowExtensions;
			SetBrushSize(static_cast<int>(Json["brush_size"].as_int(m_BrushSize)));
			m_SelectedGroup = static_cast<int>(Json["group"].as_int(0));
			m_SelectedLayer = static_cast<int>(Json["layer"].as_int(0));
			// the two stacked dockers: which workspace each shows, and how
			// the column is shared. `left_dock` and `left_width` are read as
			// the upper one, which is where the old two column layout put the
			// workspace that is not the palette.
			const auto ReadPanel = [&Json](const char *pKey, dock_panel Fallback) {
				const int Value = static_cast<int>(Json[pKey].as_int(static_cast<int>(Fallback)));
				if(Value < 0 || Value >= static_cast<int>(dock_panel::count))
					return Fallback;
				return static_cast<dock_panel>(Value);
			};
			m_RightDock[1] = ReadPanel("lower_dock", ReadPanel("left_dock", dock_panel::layers));
			m_RightDock[0] = ReadPanel("upper_dock", ReadPanel("right_dock", dock_panel::tiles));
			m_aDocks[static_cast<std::size_t>(m_RightDock[0])].m_Width =
				static_cast<float>(Json["right_width"].as_double(Json["left_width"].as_double(288.0)));
			m_RightSplit = static_cast<float>(Json["right_split"].as_double(300.0));
			m_bUpperCollapsed = Json["upper_collapsed"].as_bool(false);
			m_bLowerCollapsed = Json["lower_collapsed"].as_bool(false);
			SetEnvelopeTime(static_cast<float>(Json["envelope_time"].as_double(0.0)));

			/*
				The visibility and lock flags are written per group and layer in
				map order; a project written for a different map shape is applied
				as far as it reaches and the rest keeps the defaults, so a stale
				state can never point a flag at a layer it was not meant for.
			*/
			// a tab this build does not know keeps the default order
			const auto LoadTabs = [](const base::json &Array, std::vector<dock_panel> &Order) {
				if(!Array.is_array() || Array.size() == 0)
					return;
				std::vector<dock_panel> Loaded;
				for(std::size_t i = 0; i < Array.size(); i++)
				{
					const int Value = static_cast<int>(Array.at(i).as_int(-1));
					if(Value < 0 || Value >= static_cast<int>(dock_panel::count))
						return;
					Loaded.push_back(static_cast<dock_panel>(Value));
				}
				Order = std::move(Loaded);
			};
			LoadTabs(Json["tabs_upper"], m_vRightTabs);
			LoadTabs(Json["tabs_lower"], m_vLowerTabs);

			const base::json &Views = Json["views"];
			if(Views.is_array())
				for(std::size_t g = 0; g < m_vViewStates.size() && g < Views.size(); g++)
					for(std::size_t l = 0; l < m_vViewStates[g].size() && l < Views.at(g).size(); l++)
					{
						const base::json &Entry = Views.at(g).at(l);
						if(!Entry.is_object())
							continue;
						m_vViewStates[g][l].m_Visible = Entry["visible"].as_bool(m_vViewStates[g][l].m_Visible);
						m_vViewStates[g][l].m_Locked = Entry["locked"].as_bool(m_vViewStates[g][l].m_Locked);
					}
		}
		else
		{
			FitToMap();
		}
		ClampSelection();

		SetStatus(  base::FormatRuntime(Tr("opened the project '{}' ({})"), m_Project.m_Meta.m_Name.c_str(),
			storage::path(Path).filename().string().c_str())  );
		return true;
	}

	base::json CEditor::EditorState() const
	{
		// the editor state travels with the project: the camera, the selection
		// and the workspace layout, so reopening resumes the session
		base::json Json = base::json::object();
		Json["zoom"] = static_cast<double>(m_View.m_Zoom);
		Json["center_x"] = static_cast<double>(m_View.m_Center.x);
		Json["center_y"] = static_cast<double>(m_View.m_Center.y);
		Json["grid"] = m_bGrid;
		Json["show_detail"] = m_bShowDetail;
		Json["allow_extensions"] = m_bAllowExtensions;
		Json["brush_size"] = m_BrushSize;
		Json["brush_tile"] = m_BrushTile;
		Json["group"] = m_SelectedGroup;
		Json["layer"] = m_SelectedLayer;
		// the two stacked dockers, written with the same keys the loader reads
		Json["upper_dock"] = static_cast<int>(m_RightDock[0]);
		Json["lower_dock"] = static_cast<int>(m_RightDock[1]);
		Json["right_width"] = static_cast<double>(m_aDocks[static_cast<std::size_t>(m_RightDock[0])].m_Width);
		Json["right_split"] = static_cast<double>(m_RightSplit);
		Json["upper_collapsed"] = m_bUpperCollapsed;
		Json["lower_collapsed"] = m_bLowerCollapsed;
		Json["envelope_time"] = static_cast<double>(m_EnvelopeTime);

		// the per layer visibility and lock flags, in map order so that a
		// reordered group does not silently carry the wrong flags
		base::json Views = base::json::array();
		for(const std::vector<CLayerView> &States : m_vViewStates)
		{
			base::json Group = base::json::array();
			for(const CLayerView &State : States)
			{
				base::json Entry = base::json::object();
				Entry["visible"] = State.m_Visible;
				Entry["locked"] = State.m_Locked;
				Group.push_back(std::move(Entry));
			}
			Views.push_back(std::move(Group));
		}
		Json["views"] = std::move(Views);

		/*
			The tab order of each dock travels with the project: a map maker who
			put Envelopes first on the left expects that again tomorrow, and the
			order is the only part of the workspace that is not derived from the
			window size.
		*/
		const auto TabOrder = [](const std::vector<dock_panel> &Order) {
			base::json Array = base::json::array();
			for(const dock_panel Panel : Order)
				Array.push_back(static_cast<int>(Panel));
			return Array;
		};
		Json["tabs_upper"] = TabOrder(Tabs(dock_slot::right_upper));
		Json["tabs_lower"] = TabOrder(Tabs(dock_slot::right_lower));
		return Json;
	}

	bool CEditor::SaveProject(const std::string &Path)
	{
		m_Project.m_Map = m_Map;
		m_Project.m_Editor = m_Project.MergeEditorState(EditorState());
		m_Project.m_Rules = rules::ToJson(m_RuleSet);
		/*
			The target the editor is aimed at wins, but a session that never
			chose one keeps the target the project already carried: that is what
			makes `project.save` on a one-shot invocation preserve the version
			the project was created for instead of falling back to 0.8.
		*/
		if(m_Target != map_target::tee08)
			m_Project.m_Meta.m_Target = GetTargetProfile(m_Target).m_pId;
		else if(const std::optional<map_target> Stored = TargetFromId(m_Project.m_Meta.m_Target))
			m_Target = *Stored;
		/*
			The target decided above is what names the mapres set, so the two
			cannot drift: a project saved from a session that never loaded a
			bare map would otherwise keep whatever version the editor started
			with, and `project.save --target 06` has to record 06.
		*/
		m_TargetVersionId = VersionIdForTarget(m_Target);
		const std::string Assets = VersionForSave();
		m_Project.m_Meta.m_Author = m_Map.m_aAuthor;
		if(m_Project.m_Meta.m_Name.empty())
			SetProjectName(storage::path(Path).stem().string());
		m_Project.m_Meta.m_Assets = Assets;
		m_Project.m_Meta.m_Updated = std::to_string(static_cast<long long>(std::time(nullptr)));
		if(m_Project.m_Meta.m_Id.empty())
		{
			// a project that came from a plain map has no identity yet
			CProject Fresh;
			Fresh.InitNew(m_Project.m_Meta.m_Name.empty() ? "untitled" : m_Project.m_Meta.m_Name,
				GetTargetProfile(m_Target).m_pId);
			m_Project.m_Meta.m_Id = Fresh.m_Meta.m_Id;
			m_Project.m_Meta.m_Created = Fresh.m_Meta.m_Created;
		}

		std::string Error;
		if(!m_Project.SaveToFile(Path, &Error))
		{
			SetStatus(base::FormatRuntime(Tr("saving the project failed: {}"), Error));
			return false;
		}
		m_SourcePath = Path;
		m_bSessionSeeded = true;
		m_Dirty = false;
		SetStatus(  base::FormatRuntime(Tr("saved the project '{}'"), storage::path(Path).filename().string().c_str())  );
		return true;
	}

	// -------------------------------------------------------------------- rules
	bool CEditor::LoadRules(const std::string &Path)
	{
		rules::CRuleSet Rules;
		std::string Error;
		if(!rules::LoadFromFile(Rules, Path, &Error))
		{
			SetStatus(base::FormatRuntime(Tr("cannot load the rules: {}"), Error));
			return false;
		}
		m_RuleSet = std::move(Rules);
		m_RuleConfig = 0;
		m_RuleRun = 0;
		m_RuleIndex = 0;
		m_Dirty = true;
		SetStatus(  base::FormatRuntime(Tr("loaded {:d} configurations, {:d} rules from {}"),
			static_cast<int>(m_RuleSet.m_vConfigs.size()), m_RuleSet.RuleCount(),
			storage::path(Path).filename().string().c_str())  );
		return true;
	}

	bool CEditor::SaveRules(const std::string &Path)
	{
		std::string Error;
		if(!rules::SaveToFile(m_RuleSet, Path, &Error))
		{
			SetStatus(base::FormatRuntime(Tr("saving the rules failed: {}"), Error));
			return false;
		}
		SetStatus(  base::FormatRuntime(Tr("saved {:d} rules to {}"), m_RuleSet.RuleCount(),
			storage::path(Path).filename().string().c_str())  );
		return true;
	}

	void CEditor::NewRuleSet()
	{
		m_RuleSet = rules::CRuleSet{};
		m_RuleSet.m_Name = m_Project.m_Meta.m_Name;
		m_RuleConfig = 0;
		m_RuleRun = 0;
		m_RuleIndex = 0;
		AddRuleConfig();
		m_Dirty = true;
	}

	void CEditor::NewRuleSetOp() { NewRuleSet(); }

	void CEditor::AddRuleConfig()
	{
		rules::CConfiguration Config;
		Config.m_Name = base::Format("config {:d}", static_cast<int>(m_RuleSet.m_vConfigs.size()) + 1);
		rules::CRun Run;
		Run.m_Name = "run 1";
		// reading the first plain tile layer is what almost every rule does
		Run.m_ReadSlot = 6;
		Config.m_vRuns.push_back(std::move(Run));
		m_RuleSet.m_vConfigs.push_back(std::move(Config));
		m_RuleConfig = static_cast<int>(m_RuleSet.m_vConfigs.size()) - 1;
		m_RuleRun = 0;
		m_RuleIndex = 0;
		m_Dirty = true;
	}

	void CEditor::AddRuleRun()
	{
		if(m_RuleConfig < 0 || m_RuleConfig >= static_cast<int>(m_RuleSet.m_vConfigs.size()))
			return;
		rules::CRun Run;
		Run.m_Name = base::Format("run {:d}", static_cast<int>(m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)].m_vRuns.size()) + 1);
		Run.m_ReadSlot = 6;
		m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)].m_vRuns.push_back(std::move(Run));
		m_RuleRun = static_cast<int>(m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)].m_vRuns.size()) - 1;
		m_RuleIndex = 0;
		m_Dirty = true;
	}

	void CEditor::AddRule()
	{
		if(m_RuleConfig < 0 || m_RuleConfig >= static_cast<int>(m_RuleSet.m_vConfigs.size()))
			AddRuleConfig();
		rules::CConfiguration &Config = m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)];
		if(Config.m_vRuns.empty())
			AddRuleRun();
		rules::CRun &Run = Config.m_vRuns[static_cast<std::size_t>(base::clamp(m_RuleRun, 0, static_cast<int>(Config.m_vRuns.size()) - 1))];
		rules::CIndexRule Rule;
		// a new rule writes what the brush is aimed at, which is the least
		// surprising default and the one the DDRace editor uses too
		Rule.m_Tile = m_BrushTile;
		Rule.m_Flag = m_BrushFlags;
		Rule.m_Name = base::Format("rule {:d}", static_cast<int>(Run.m_vIndexRules.size()) + 1);
		Run.m_vIndexRules.push_back(std::move(Rule));
		m_RuleIndex = static_cast<int>(Run.m_vIndexRules.size()) - 1;
		m_Dirty = true;
	}

	void CEditor::DeleteRuleSelection()
	{
		if(m_RuleConfig < 0 || m_RuleConfig >= static_cast<int>(m_RuleSet.m_vConfigs.size()))
			return;
		rules::CConfiguration &Config = m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)];
		if(m_RuleRun < 0 || m_RuleRun >= static_cast<int>(Config.m_vRuns.size()))
			return;
		rules::CRun &Run = Config.m_vRuns[static_cast<std::size_t>(m_RuleRun)];
		if(m_RuleIndex < 0 || m_RuleIndex >= static_cast<int>(Run.m_vIndexRules.size()))
			return;
		Run.m_vIndexRules.erase(Run.m_vIndexRules.begin() + m_RuleIndex);
		m_RuleIndex = std::max(0, std::min(m_RuleIndex, static_cast<int>(Run.m_vIndexRules.size()) - 1));
		m_Dirty = true;
	}

	void CEditor::SetRuleSelection(int Config, int Run, int Rule)
	{
		m_RuleConfig = base::clamp(Config, 0, std::max(0, static_cast<int>(m_RuleSet.m_vConfigs.size()) - 1));
		m_RuleRun = std::max(Run, 0);
		m_RuleIndex = std::max(Rule, 0);
	}

	/*
		The layer addressing of rules.h: the engine slot of a layer. Slot 0 is
		the game layer, 1..5 are the DDRace physics layers, and the plain tile
		layers are numbered from 6 in the order they appear in the map. A quad
		or sound layer has no slot, so a rule cannot name it.
	*/
	int CEditor::SlotForLayer(int Group, int Layer) const
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return -1;
		const CGroup &GroupRef = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer < 0 || Layer >= static_cast<int>(GroupRef.m_vLayers.size()))
			return -1;
		const CLayer &LayerRef = GroupRef.m_vLayers[static_cast<std::size_t>(Layer)];
		if(!LayerRef.IsTiles())
			return -1;
		if(LayerRef.IsGameLayer())
			return 0;
		if(LayerRef.m_Flags & TILESLAYERFLAG_TELE)
			return 1;
		if(LayerRef.m_Flags & TILESLAYERFLAG_SPEEDUP)
			return 2;
		if(LayerRef.m_Flags & TILESLAYERFLAG_FRONT)
			return 3;
		if(LayerRef.m_Flags & TILESLAYERFLAG_SWITCH)
			return 4;
		if(LayerRef.m_Flags & TILESLAYERFLAG_TUNE)
			return 5;

		// the plain tile layers in map order
		int Slot = 6;
		for(const CGroup &Other : m_Map.m_vGroups)
			for(std::size_t l = 0; l < Other.m_vLayers.size(); l++)
			{
				const CLayer &Candidate = Other.m_vLayers[l];
				if(!Candidate.IsTiles() || Candidate.IsGameLayer() || Candidate.IsPhysicsLayer())
					continue;
				if(&Other == &GroupRef && static_cast<int>(l) == Layer)
					return Slot;
				Slot++;
			}
		return -1;
	}

	bool CEditor::LayerForSlot(int Slot, int &Group, int &Layer) const
	{
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
			for(std::size_t l = 0; l < m_Map.m_vGroups[g].m_vLayers.size(); l++)
				if(SlotForLayer(static_cast<int>(g), static_cast<int>(l)) == Slot)
				{
					Group = static_cast<int>(g);
					Layer = static_cast<int>(l);
					return true;
				}
		return false;
	}

	bool CEditor::ApplyRules(int ConfigIndex, int TargetSlot, int Seed, std::string *pError)
	{
		const auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = Message;
			SetStatus(base::FormatRuntime(Tr("rules: {}"), Message));
			return false;
		};
		if(m_RuleSet.m_vConfigs.empty())
			return Fail("this project has no autorules yet");

		int Group = 0, Layer = 0;
		if(!LayerForSlot(TargetSlot, Group, Layer))
			return Fail(base::Format("slot {:d} is not a tile layer of this map", TargetSlot));
		CLayer *pTarget = LayerAt(Group, Layer);
		if(pTarget == nullptr || !pTarget->IsTiles())
			return Fail("the target slot is not a tile layer");
		if(pTarget->IsPhysicsLayer())
			return Fail("the physics layers carry a payload, not a tile array: apply rules to a plain tile layer");

		/*
			The rule engine reads a plane of indices and a plane of flags per
			layer, so the tiles are copied out of the map first and written back
			afterwards. That also keeps the engine free of CTile and lets it be
			tested without a map at all.
		*/
		int MaxSlot = 0;
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
			for(std::size_t l = 0; l < m_Map.m_vGroups[g].m_vLayers.size(); l++)
				MaxSlot = std::max(MaxSlot, SlotForLayer(static_cast<int>(g), static_cast<int>(l)));

		const std::size_t Slots = static_cast<std::size_t>(MaxSlot) + 1;
		std::vector<std::vector<std::uint8_t>> TilePlanes(Slots);
		std::vector<std::vector<std::uint8_t>> FlagPlanes(Slots);
		std::vector<rules::CRuleLayer> ReadLayers(Slots);
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
			for(std::size_t l = 0; l < m_Map.m_vGroups[g].m_vLayers.size(); l++)
			{
				const int Slot = SlotForLayer(static_cast<int>(g), static_cast<int>(l));
				if(Slot < 0)
					continue;
				const CLayer &Other = m_Map.m_vGroups[g].m_vLayers[l];
				const std::size_t Count = Other.m_vTiles.size();
				const std::size_t At = static_cast<std::size_t>(Slot);
				TilePlanes[At].resize(Count);
				FlagPlanes[At].resize(Count);
				for(std::size_t t = 0; t < Count; t++)
				{
					TilePlanes[At][t] = Other.m_vTiles[t].m_Index;
					FlagPlanes[At][t] = Other.m_vTiles[t].m_Flags;
				}
				ReadLayers[At] = rules::CRuleLayer{TilePlanes[At].data(), FlagPlanes[At].data(), Other.m_Width, Other.m_Height};
			}

		// one snapshot of the whole map, because a rule run can rewrite a layer
		PushUndo(Group, Layer, undo_scope::whole);

		const std::size_t TargetAt = static_cast<std::size_t>(TargetSlot);
		const rules::CApplyResult Result = ConfigIndex < 0 ?
			rules::ApplyAll(m_RuleSet, ReadLayers[TargetAt], ReadLayers, Seed) :
			rules::Apply(m_RuleSet, ConfigIndex, ReadLayers[TargetAt], ReadLayers, Seed);
		if(!Result.m_bOk)
		{
			// the snapshot is not wanted when nothing happened
			if(!m_vUndo.empty())
				m_vUndo.pop_back();
			return Fail(Result.m_Error);
		}

		// the engine wrote into the plane it was given; put it back
		CLayer &Target = *pTarget;
		for(std::size_t t = 0; t < Target.m_vTiles.size() && t < TilePlanes[TargetAt].size(); t++)
		{
			Target.m_vTiles[t].m_Index = TilePlanes[TargetAt][t];
			Target.m_vTiles[t].m_Flags = FlagPlanes[TargetAt][t];
		}
		m_Dirty = true;
		m_LastRuleSeed = Result.m_Seed;
		SetStatus(  base::FormatRuntime(Tr("autorules wrote {:d} tiles over {:d} runs in {} (seed {:d})"),
			Result.m_TilesWritten, Result.m_RunsApplied,
			Target.m_Name.empty() ? Tr("the target layer") : Target.m_Name.c_str(), Result.m_Seed)  );
		return true;
	}

	// ------------------------------------------------------------ game entities
	bool CEditor::GameEntitiesAllowed() const
	{
		// the extensions switch is what unlocks the DDRace entities, so a
		// mapmaker can keep them out of a vanilla map by turning it off
		return m_bAllowExtensions || GetTargetProfile(m_Target).HasExtension(ext_game_entities);
	}

	void CEditor::AddPhysicsLayer(int Flags)
	{
		CGroup *pGroup = SelectedGroup();
		if(pGroup == nullptr && !m_Map.m_vGroups.empty())
			pGroup = &m_Map.m_vGroups.front();
		if(pGroup == nullptr)
			return;

		for(const CLayer &Layer : pGroup->m_vLayers)
			if(Layer.IsTiles() && (Layer.m_Flags & Flags) == Flags)
			{
				SetStatus( Tr("that physics layer already exists") );
				return;
			}

		// every tile layer of a map shares one size, so the new layer matches
		int Width = 50, Height = 50;
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
				if(Layer.IsTiles())
				{
					Width = Layer.m_Width;
					Height = Layer.m_Height;
				}

		const char *pName = (Flags & TILESLAYERFLAG_TELE) ? "Tele" :
			(Flags & TILESLAYERFLAG_SPEEDUP) ? "Speedup" :
			(Flags & TILESLAYERFLAG_FRONT) ? "Front" :
			(Flags & TILESLAYERFLAG_SWITCH) ? "Switch" :
			(Flags & TILESLAYERFLAG_TUNE) ? "Tune" : "Physics";

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		CLayer &Layer = m_Map.NewTileLayer(*pGroup, Width, Height, -1);
		Layer.m_Name = pName;
		Layer.m_Flags = Flags;
		Layer.m_Color = CColor{255, 255, 255, 255};
		// the payload is one element per tile, sized by the layer kind
		const int Element = (Flags & TILESLAYERFLAG_TELE) ? 2 : (Flags & TILESLAYERFLAG_SPEEDUP) ? 6 :
			(Flags & TILESLAYERFLAG_FRONT) ? 4 : (Flags & TILESLAYERFLAG_SWITCH) ? 4 : 2;
		const std::size_t Bytes = Layer.TileCount() * static_cast<std::size_t>(Element);
		if(Flags & TILESLAYERFLAG_TELE)
			Layer.m_vTele.assign(Bytes, std::byte{0});
		else if(Flags & TILESLAYERFLAG_SPEEDUP)
			Layer.m_vSpeedup.assign(Bytes, std::byte{0});
		else if(Flags & TILESLAYERFLAG_FRONT)
			Layer.m_vFront.assign(Bytes, std::byte{0});
		else if(Flags & TILESLAYERFLAG_SWITCH)
			Layer.m_vSwitch.assign(Bytes, std::byte{0});
		else if(Flags & TILESLAYERFLAG_TUNE)
			Layer.m_vTune.assign(Bytes, std::byte{0});
		Layer.m_SourceHadPhysics = true;

		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedLayer = static_cast<int>(pGroup->m_vLayers.size()) - 1;
		m_Dirty = true;
		SetStatus(  base::FormatRuntime(Tr("added the {} physics layer"), pName)  );
	}

	// ---------------------------------------------------------------- envelopes
	int CEditor::SelectedEnvelopeIndex() const
	{
		if(m_SelectedEnvelope >= 0 && m_SelectedEnvelope < static_cast<int>(m_Map.m_vEnvelopes.size()))
			return m_SelectedEnvelope;
		const int Color = SelectedColorEnvelope();
		if(Color >= 0)
			return Color;
		return SelectedPositionEnvelope();
	}

	CEnvelope *CEditor::SelectedEnvelope()
	{
		const int Index = SelectedEnvelopeIndex();
		if(Index < 0)
			return nullptr;
		return &m_Map.m_vEnvelopes[static_cast<std::size_t>(Index)];
	}

	const CEnvelope *CEditor::SelectedEnvelope() const { return const_cast<CEditor *>(this)->SelectedEnvelope(); }

	int CEditor::SelectedColorEnvelope() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return -1;
		if(pLayer->IsQuads())
		{
			if(m_vSelectedQuads.empty())
				return -1;
			const int Index = m_vSelectedQuads.front();
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				return -1;
			return pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_ColorEnv;
		}
		return pLayer->m_ColorEnv;
	}

	int CEditor::SelectedPositionEnvelope() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads() || m_vSelectedQuads.empty())
			return -1;
		const int Index = m_vSelectedQuads.front();
		if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
			return -1;
		return pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_PosEnv;
	}

	int CEditor::AddEnvelope(int Channels)
	{
		CEnvelope Envelope;
		Envelope.m_Channels = base::clamp(Channels, 1, 4);
		Envelope.m_Name = base::Format("envelope {:d}", static_cast<int>(m_Map.m_vEnvelopes.size()) + 1);
		// one point is a constant, which is what a fresh envelope should be
		CEnvelopePoint Point;
		Point.m_Time = 0;
		Point.m_Curvetype = CURVETYPE_LINEAR;
		for(int c = 0; c < 4; c++)
			Point.m_aValues[c] = 0;
		Envelope.m_vPoints.push_back(Point);
		m_Map.m_vEnvelopes.push_back(std::move(Envelope));
		m_SelectedEnvelope = static_cast<int>(m_Map.m_vEnvelopes.size()) - 1;
		m_SelectedEnvelopePoint = 0;
		m_Dirty = true;
		return m_SelectedEnvelope;
	}

	int CEditor::EnsureEnvelopeSelected()
	{
		const int Existing = SelectedEnvelopeIndex();
		if(Existing >= 0)
		{
			m_SelectedEnvelope = Existing;
			return Existing;
		}
		// nothing points at an envelope: make one that fits the selection and
		// attach it, which is the only way the workspace can start from nothing
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return -1;
		const bool bQuads = pLayer->IsQuads();
		const int Index = AddEnvelope(bQuads && !m_vSelectedQuads.empty() ? 4 : 4);
		if(bQuads && !m_vSelectedQuads.empty())
		{
			const int Quad = m_vSelectedQuads.front();
			if(Quad >= 0 && Quad < static_cast<int>(pLayer->m_vQuads.size()))
				pLayer->m_vQuads[static_cast<std::size_t>(Quad)].m_ColorEnv = Index;
		}
		else
		{
			pLayer->m_ColorEnv = Index;
		}
		SetStatus(  base::FormatRuntime(Tr("added the envelope '{}'"), m_Map.m_vEnvelopes[static_cast<std::size_t>(Index)].m_Name.c_str())  );
		return Index;
	}

	void CEditor::SetEnvelopePointTime(int PointIndex, int Time)
	{
		CEnvelope *pEnvelope = SelectedEnvelope();
		if(pEnvelope == nullptr || PointIndex < 0 || PointIndex >= static_cast<int>(pEnvelope->m_vPoints.size()))
			return;
		// a point has to stay between its neighbours or the evaluation would
		// interpolate backwards
		const int Low = PointIndex > 0 ? pEnvelope->m_vPoints[static_cast<std::size_t>(PointIndex - 1)].m_Time : 0;
		int High = 0x7fffffff;
		if(PointIndex + 1 < static_cast<int>(pEnvelope->m_vPoints.size()))
			High = pEnvelope->m_vPoints[static_cast<std::size_t>(PointIndex + 1)].m_Time;
		pEnvelope->m_vPoints[static_cast<std::size_t>(PointIndex)].m_Time = base::clamp(Time, Low, std::max(Low, High));
		m_Dirty = true;
	}

	void CEditor::SetEnvelopePointValue(int PointIndex, int Channel, int Value)
	{
		CEnvelope *pEnvelope = SelectedEnvelope();
		if(pEnvelope == nullptr || PointIndex < 0 || PointIndex >= static_cast<int>(pEnvelope->m_vPoints.size()))
			return;
		if(Channel < 0 || Channel >= 4)
			return;
		pEnvelope->m_vPoints[static_cast<std::size_t>(PointIndex)].m_aValues[Channel] = Value;
		m_Dirty = true;
	}

	void CEditor::AddEnvelopePoint(float Time)
	{
		CEnvelope *pEnvelope = SelectedEnvelope();
		if(pEnvelope == nullptr)
			return;
		const int TimeMs = static_cast<int>(Time * 1000.0f);
		CEnvelopePoint Point;
		Point.m_Curvetype = pEnvelope->m_vPoints.empty() ? CURVETYPE_LINEAR : pEnvelope->m_vPoints.back().m_Curvetype;
		// the value the envelope already has there, so adding a point does not
		// change the animation until it is dragged
		CEnvelopeSample Sample;
		for(int c = 0; c < 4; c++)
			Sample[c] = 0.0f;
		EvaluateEnvelope(*pEnvelope, static_cast<float>(TimeMs) / 1000.0f, Sample, 4);
		Point.m_Time = std::max(TimeMs, 0);
		for(int c = 0; c < 4; c++)
			Point.m_aValues[c] = static_cast<int>(std::lround(Sample[c] * FixedPointScale));
		pEnvelope->m_vPoints.push_back(Point);
		std::sort(pEnvelope->m_vPoints.begin(), pEnvelope->m_vPoints.end(),
			[](const CEnvelopePoint &A, const CEnvelopePoint &B) { return A.m_Time < B.m_Time; });
		for(std::size_t i = 0; i < pEnvelope->m_vPoints.size(); i++)
			if(pEnvelope->m_vPoints[i].m_Time == Point.m_Time && pEnvelope->m_vPoints[i].m_aValues[0] == Point.m_aValues[0])
			{
				m_SelectedEnvelopePoint = static_cast<int>(i);
				break;
			}
		m_Dirty = true;
	}

	void CEditor::DeleteEnvelopePoint(int PointIndex)
	{
		CEnvelope *pEnvelope = SelectedEnvelope();
		if(pEnvelope == nullptr || PointIndex < 0 || PointIndex >= static_cast<int>(pEnvelope->m_vPoints.size()))
			return;
		// an envelope needs at least one point to mean anything
		if(pEnvelope->m_vPoints.size() <= 1)
		{
			SetStatus( Tr("an envelope keeps its last point") );
			return;
		}
		pEnvelope->m_vPoints.erase(pEnvelope->m_vPoints.begin() + PointIndex);
		m_SelectedEnvelopePoint = std::min(PointIndex, static_cast<int>(pEnvelope->m_vPoints.size()) - 1);
		m_Dirty = true;
	}
} // namespace game
