/*
	The methods of the administrative interface.

	One handler per row of the table at the bottom of this file. The table is
	the single source of truth: `agent.describe` prints it, the dispatcher
	looks a method up in it, and the command line builds its subcommands from
	it, so a new operation is added in exactly one place.

	The handlers work on the editor, never on a copy of it, which is what makes
	an agent session and a window indistinguishable from the model's point of
	view.
*/

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "base/define.h"
#include "base/logger.h"
#include "base/png.h"
#include "base/string.h"
#include "game/assets.h"
#include "game/editor/admin.h"
#include "game/editor/admin_ops.h"
#include "game/editor/editor.h"
#include "game/editor/render.h"
#include "game/map/envelope.h"

namespace game::admin::ops
{
	// ----------------------------------------------------------------- context
	bool CContext::Fail(const std::string &Message) const
	{
		m_Response.m_bOk = false;
		m_Response.m_Error = Message;
		return false;
	}

	bool CContext::Ok(base::json Result) const
	{
		m_Response.m_bOk = true;
		m_Response.m_Result = Result.is_object() ? std::move(Result) : base::json::object();
		return true;
	}

	namespace
	{
		// the shared success payload: what changed, and the new selection
		base::json Changed(CEditor &Editor, const char *pWhat)
		{
			base::json Result = base::json::object();
			Result["changed"] = pWhat;
			Result["selection"] = SelectionToJson(Editor);
			Result["dirty"] = Editor.Dirty();
			return Result;
		}

		// a layer by index, or an error
		CLayer *FindLayer(CEditor &Editor, int Group, int Layer, std::string &Error)
		{
			CLayer *pLayer = Editor.Layer(Group, Layer);
			if(pLayer == nullptr)
				Error = base::Format("there is no layer {:d} of group {:d}", Layer, Group);
			return pLayer;
		}

		// a target id to a target, or an error
		// A "target" may be left out, or given as the current target's id; both
		// mean "keep what the session already has", which is what an agent that
		// only wants to change the size expects.
		bool TargetFromParam(const base::json &Value, map_target &Out, std::string *pError, bool bKeepWhenMissing = true)
		{
			if(bKeepWhenMissing && (!Value.is_string() || Value.as_string().empty()))
				return true;
			const std::string Id = Value.is_string() ? Value.as_string() : std::string();
			const std::optional<map_target> Target = TargetFromId(Id);
			if(!Target.has_value())
			{
				if(pError != nullptr)
					*pError = "unknown target '" + Id + "', expected 06, 07, ddnet, ddnet06, ddnet07 or 08";
				return false;
			}
			Out = *Target;
			return true;
		}

		// the layer kind to a type and flag set, for `layers.add`
		bool FlagsFromKind(const std::string &Kind, int &Type, int &Flags)
		{
			Type = LAYERTYPE_TILES;
			Flags = 0;
			if(Kind == "tiles" || Kind == "tile") return true;
			if(Kind == "game") { Flags = TILESLAYERFLAG_GAME; return true; }
			if(Kind == "tele" || Kind == "teleport") { Flags = TILESLAYERFLAG_TELE; return true; }
			if(Kind == "speedup") { Flags = TILESLAYERFLAG_SPEEDUP; return true; }
			if(Kind == "front") { Flags = TILESLAYERFLAG_FRONT; return true; }
			if(Kind == "switch") { Flags = TILESLAYERFLAG_SWITCH; return true; }
			if(Kind == "tune") { Flags = TILESLAYERFLAG_TUNE; return true; }
			if(Kind == "quads" || Kind == "quad") { Type = LAYERTYPE_QUADS; return true; }
			if(Kind == "sounds" || Kind == "sound") { Type = LAYERTYPE_SOUNDS; return true; }
			return false;
		}

		// a tile or game layer, or an error
		CLayer *TileLayer(CEditor &Editor, int Group, int Layer, std::string &Error)
		{
			CLayer *pLayer = Editor.Layer(Group, Layer);
			if(pLayer == nullptr)
			{
				Error = base::Format("there is no layer {:d} of group {:d}", Layer, Group);
				return nullptr;
			}
			if(!pLayer->IsTiles())
			{
				Error = base::Format("layer {:d} of group {:d} is not a tile layer", Layer, Group);
				return nullptr;
			}
			return pLayer;
		}

		// the rectangle a region parameter describes, clamped to a layer
		bool RegionFromParams(const CContext &Context, const CLayer &Layer, int &X0, int &Y0, int &X1, int &Y1, std::string &Error)
		{
			const base::json &Region = Context.Json("region");
			if(!Region.is_array() || Region.size() < 4)
			{
				X0 = 0;
				Y0 = 0;
				X1 = Layer.m_Width - 1;
				Y1 = Layer.m_Height - 1;
				return true;
			}
			const int Rx = static_cast<int>(Region.at(0).as_int());
			const int Ry = static_cast<int>(Region.at(1).as_int());
			const int Rw = static_cast<int>(Region.at(2).as_int());
			const int Rh = static_cast<int>(Region.at(3).as_int());
			X0 = std::max(Rx, 0);
			Y0 = std::max(Ry, 0);
			X1 = std::min(Rw > 0 ? Rx + Rw - 1 : Layer.m_Width - 1, Layer.m_Width - 1);
			Y1 = std::min(Rh > 0 ? Ry + Rh - 1 : Layer.m_Height - 1, Layer.m_Height - 1);
			if(X1 < X0 || Y1 < Y0)
			{
				Error = "the region does not overlap the layer";
				return false;
			}
			return true;
		}

		int FlagsFromJson(const base::json &Value)
		{
			if(Value.is_number())
				return static_cast<int>(Value.as_int());
			if(!Value.is_array())
				return 0;
			int Flags = 0;
			for(std::size_t i = 0; i < Value.size(); i++)
			{
				const std::string Name = base::ToLower(Value.at(i).as_string());
				if(Name == "hflip" || Name == "xflip")
					Flags |= TILEFLAG_HFLIP;
				else if(Name == "vflip" || Name == "yflip")
					Flags |= TILEFLAG_VFLIP;
				else if(Name == "rotate")
					Flags |= TILEFLAG_ROTATE;
				else if(Name == "opaque")
					Flags |= TILEFLAG_OPAQUE;
			}
			return Flags;
		}

		/*
			"game.tele" and friends name an entity; the table itself lives with
			the entity sheet, because the palette shows the same names beside
			the sprites.
		*/
	} // namespace

	// ------------------------------------------------------------------ agent
	namespace
	{
		bool AgentVersion(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["name"] = GAME_DISPLAY_NAME;
			Result["version"] = GAME_VERSION;
			Result["protocol"] = 1;
			Result["targets"] = base::json::array();
			for(const map_target Target : AllTargets())
				Result["targets"].push_back(std::string(GetTargetProfile(Target).m_pId));
			return Context.Ok(std::move(Result));
		}

		bool AgentDescribe(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["methods"] = Describe();
			Result["formats"] = base::json::object();
			Result["formats"]["request"] = "{\"id\": any, \"method\": string, \"params\": object}";
			Result["formats"]["response"] = "{\"id\": any, \"method\": string, \"ok\": bool, \"result\": object} or {..., \"error\": string}";
			return Context.Ok(std::move(Result));
		}

		bool AgentPing(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["pong"] = true;
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ---------------------------------------------------------------- project
	namespace
	{
		bool ProjectGet(const CContext &Context)
		{
			return Context.Ok(ProjectToJson(Context.m_Editor));
		}

		bool ProjectCreate(const CContext &Context)
		{
			const std::string Name = Context.Str("name", "untitled");
			const std::string TargetId = Context.Str("target", "08");
			std::string Error;
			map_target Target = map_target::tee08;
			if(!TargetFromParam(Context.Json("target"), Target, &Error))
				return Context.Fail(Error);
			const int Width = Context.Int("width", 50);
			const int Height = Context.Int("height", 50);

			CEditor &Editor = Context.m_Editor;
			Editor.Project().InitNew(Name, TargetId);
			Editor.Project().m_Map = CMap();
			Editor.Map() = Editor.Project().m_Map;
			Editor.Map().InitNew(Name);
			Editor.Map().m_aAuthor = Context.Str("author", "Uniteed");
			for(CGroup &Group : Editor.Map().m_vGroups)
				for(CLayer &Layer : Group.m_vLayers)
					if(Layer.IsTiles())
						Layer.Resize(std::max(Width, 4), std::max(Height, 4));
			Editor.SetTarget(Target);
			if(Context.Has("allow_extensions"))
				Editor.SetAllowExtensions(Context.Bool("allow_extensions"));
			Editor.NewMapReset();
			Editor.SetProjectName(Name);

			base::json Result = Changed(Editor, "project.create");
			Result["project"] = ProjectToJson(Editor);
			return Context.Ok(std::move(Result));
		}

		bool ProjectOpen(const CContext &Context)
		{
			const std::string NameOrPath = Context.Str("path");
			if(NameOrPath.empty())
				return Context.Fail("project.open needs a \"path\" (a project file, or a name in the projects directory)");
			const storage::path Path = ResolveProjectPath(NameOrPath);
			if(!Context.m_Editor.LoadProject(Path.string()))
				return Context.Fail(Context.m_Editor.Status());
			base::json Result = Changed(Context.m_Editor, "project.open");
			Result["project"] = ProjectToJson(Context.m_Editor);
			return Context.Ok(std::move(Result));
		}

		bool ProjectSave(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			// the name first, so the default path is built from it
			if(Context.Has("name"))
				Editor.SetProjectName(Context.Str("name"));
			if(Context.Has("target"))
			{
				std::string Error;
				map_target Target = Editor.Target();
				if(!TargetFromParam(Context.Json("target"), Target, &Error, false))
					return Context.Fail(Error);
				Editor.SetTarget(Target);
			}
			const std::string Given = Context.Str("path");
			const std::string Path = Given.empty() ?
				ProjectPath(Editor.ProjectName().empty() ? "untitled" : Editor.ProjectName()).string() : Given;
			/*
				Saving without a path means "write this project where it lives". A
				one-shot command starts from an empty editor, so writing straight
				over the file would throw the map and the rules away; the existing
				project is opened first and the session on top of it is what is
				saved. A resident session that already holds the project re-saves
				exactly what it loaded.
			*/
			if(Given.empty() && !Editor.SessionSeeded() && IsProjectFile(Path))
			{
				if(!Editor.LoadProject(Path))
					return Context.Fail(Editor.Status());
				// the name the caller asked for still wins over the stored one
				if(Context.Has("name"))
					Editor.SetProjectName(Context.Str("name"));
			}
			if(!Editor.SaveProject(Path))
				return Context.Fail(Editor.Status());
			base::json Result = Changed(Editor, "project.save");
			Result["path"] = Path;
			Result["project"] = ProjectToJson(Editor);
			return Context.Ok(std::move(Result));
		}

		bool ProjectList(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["directory"] = storage::GetProjectsDirectory().string();
			Result["projects"] = base::json::array();
			for(const CProjectMeta &Meta : ListProjects())
			{
				base::json Entry = base::json::object();
				Entry["id"] = Meta.m_Id;
				Entry["name"] = Meta.m_Name;
				Entry["author"] = Meta.m_Author;
				Entry["target"] = Meta.m_Target;
				Entry["assets"] = Meta.m_Assets;
				Entry["updated"] = Meta.m_Updated;
				Entry["path"] = ProjectPath(Meta.m_Name).string();
				Result["projects"].push_back(std::move(Entry));
			}
			return Context.Ok(std::move(Result));
		}

		bool ProjectSetMeta(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(Context.Has("name"))
				Editor.SetProjectName(Context.Str("name"));
			if(Context.Has("author"))
				Editor.Map().m_aAuthor = Context.Str("author");
			if(Context.Has("description"))
				Editor.Project().m_Meta.m_Description = Context.Str("description");
			if(Context.Has("version"))
				Editor.Map().m_aVersion = Context.Str("version");
			if(Context.Has("credits"))
				Editor.Map().m_aCredits = Context.Str("credits");
			if(Context.Has("license"))
				Editor.Map().m_aLicense = Context.Str("license");
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "project.set_meta"));
		}

		bool ProjectSetTarget(const CContext &Context)
		{
			if(!Context.Has("target"))
				return Context.Fail("project.set_target needs a \"target\"");
			std::string Error;
			map_target Target = map_target::tee08;
			if(!TargetFromParam(Context.Json("target"), Target, &Error, false))
				return Context.Fail(Error);
			Context.m_Editor.SetTarget(Target);
			base::json Result = Changed(Context.m_Editor, "project.set_target");
			Result["target"] = std::string(GetTargetProfile(Target).m_pId);
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ------------------------------------------------------------------ files
	namespace
	{
		bool FileOpen(const CContext &Context)
		{
			const std::string Path = Context.Str("path");
			if(Path.empty())
				return Context.Fail("file.open needs a \"path\"");
			if(IsProjectFile(Path) || base::EndsWith(Path, CUnitwpPackage::Extension))
			{
				if(!Context.m_Editor.LoadProject(Path))
					return Context.Fail(Context.m_Editor.Status());
			}
			else if(!Context.m_Editor.OpenFile(Path))
				return Context.Fail(Context.m_Editor.Status());
			return Context.Ok(Changed(Context.m_Editor, "file.open"));
		}

		bool FileImportRules(const CContext &Context)
		{
			const std::string Path = Context.Str("path");
			if(Path.empty())
				return Context.Fail("rules.import needs a \"path\"");
			if(!Context.m_Editor.LoadRules(Path))
				return Context.Fail(Context.m_Editor.Status());
			base::json Result = Changed(Context.m_Editor, "rules.import");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// --------------------------------------------------------------- map shape
	namespace
	{
		bool MapGet(const CContext &Context)
		{
			return Context.Ok(MapToJson(Context.m_Editor, Context.Bool("tiles", false)));
		}

		bool MapSetInfo(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(Context.Has("author"))
				Editor.Map().m_aAuthor = Context.Str("author");
			if(Context.Has("version"))
				Editor.Map().m_aVersion = Context.Str("version");
			if(Context.Has("credits"))
				Editor.Map().m_aCredits = Context.Str("credits");
			if(Context.Has("license"))
				Editor.Map().m_aLicense = Context.Str("license");
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "map.set_info"));
		}

		bool MapSetExtensions(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const bool bAllow = Context.Bool("allow", true);
			Editor.SetAllowExtensions(bAllow);
			// the editor warns about what a target cannot express; with the
			// switch on it says so instead of dropping the layers silently
			base::json Result = Changed(Editor, "map.set_extensions");
			Result["allow_extensions"] = bAllow;
			Result["warnings"] = base::json::array();
			for(const std::string &Warning : Editor.Map().CompatibilityWarnings(Editor.Target()))
				Result["warnings"].push_back(Warning);
			return Context.Ok(std::move(Result));
		}

		bool MapValidate(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			base::json Result = base::json::object();
			base::json Problems = base::json::array();

			// the checks an external verifier would make, said in the same words
			if(Editor.Map().m_vGroups.empty())
				Problems.push_back("the map has no groups");
			bool bGame = false;
			int MinWidth = 0, MinHeight = 0;
			bool bFirst = true;
			for(std::size_t g = 0; g < Editor.Map().m_vGroups.size(); g++)
				for(std::size_t l = 0; l < Editor.Map().m_vGroups[g].m_vLayers.size(); l++)
				{
					const CLayer &Layer = Editor.Map().m_vGroups[g].m_vLayers[l];
					if(Layer.IsGameLayer())
						bGame = true;
					if(!Layer.IsTiles())
						continue;
					if(Layer.m_vTiles.size() != Layer.TileCount())
						Problems.push_back(base::Format("group {:d} layer {:d} has {:d} tiles but is {:d}x{:d}",
							g, l, Layer.m_vTiles.size(), Layer.m_Width, Layer.m_Height));
					if(Layer.m_Image >= static_cast<int>(Editor.Map().m_vImages.size()))
						Problems.push_back(base::Format("group {:d} layer {:d} uses image {:d}, which does not exist",
							g, l, Layer.m_Image));
					if(bFirst)
					{
						MinWidth = Layer.m_Width;
						MinHeight = Layer.m_Height;
						bFirst = false;
					}
					else if(Layer.m_Width != MinWidth || Layer.m_Height != MinHeight)
						Problems.push_back(base::Format("group {:d} layer {:d} is {:d}x{:d} where the first tile layer is {:d}x{:d}",
							g, l, Layer.m_Width, Layer.m_Height, MinWidth, MinHeight));
					if(Layer.m_ColorEnv >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
						Problems.push_back(base::Format("group {:d} layer {:d} points at envelope {:d}, which does not exist",
							g, l, Layer.m_ColorEnv));
				}
			if(!bGame)
				Problems.push_back("the map has no game layer");
			for(const CEnvelope &Envelope : Editor.Map().m_vEnvelopes)
				for(std::size_t p = 1; p < Envelope.m_vPoints.size(); p++)
					if(Envelope.m_vPoints[p].m_Time < Envelope.m_vPoints[p - 1].m_Time)
					{
						Problems.push_back("an envelope has points out of time order");
						break;
					}

			Result["ok"] = Problems.size() == 0;
			Result["problems"] = std::move(Problems);
			Result["warnings"] = base::json::array();
			for(const std::string &Warning : Editor.Map().CompatibilityWarnings(Editor.Target()))
				Result["warnings"].push_back(Warning);
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// -------------------------------------------------------- groups and layers
	namespace
	{
		bool GroupsList(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			base::json Result = base::json::object();
			base::json Groups = base::json::array();
			for(std::size_t g = 0; g < Editor.Map().m_vGroups.size(); g++)
			{
				const CGroup &Group = Editor.Map().m_vGroups[g];
				base::json Entry = base::json::object();
				Entry["index"] = static_cast<int>(g);
				Entry["name"] = Group.m_Name;
				Entry["parallax"] = base::json::array();
				Entry["parallax"].push_back(Group.m_ParallaxX);
				Entry["parallax"].push_back(Group.m_ParallaxY);
				Entry["offset"] = base::json::array();
				Entry["offset"].push_back(Group.m_OffsetX);
				Entry["offset"].push_back(Group.m_OffsetY);
				Entry["clipping"] = Group.m_UseClipping != 0;
				Entry["layers"] = static_cast<int>(Group.m_vLayers.size());
				Groups.push_back(std::move(Entry));
			}
			Result["groups"] = std::move(Groups);
			Result["selection"] = SelectionToJson(Editor);
			return Context.Ok(std::move(Result));
		}

		bool GroupsAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			Editor.AddGroupOp(Context.Str("name", "Group"));
			return Context.Ok(Changed(Editor, "groups.add"));
		}

		bool GroupsSet(const CContext &Context)
		{
			const int Index = Context.Int("group", -1);
			if(Index < 0 || Index >= static_cast<int>(Context.m_Editor.Map().m_vGroups.size()))
				return Context.Fail("groups.set needs a valid \"group\"");
			CGroup &Group = Context.m_Editor.Map().m_vGroups[static_cast<std::size_t>(Index)];
			if(Context.Has("name"))
				Group.m_Name = Context.Str("name");
			const base::json &Parallax = Context.Json("parallax");
			if(Parallax.is_array() && Parallax.size() >= 2)
			{
				Group.m_ParallaxX = static_cast<int>(Parallax.at(0).as_int(100));
				Group.m_ParallaxY = static_cast<int>(Parallax.at(1).as_int(100));
			}
			const base::json &Offset = Context.Json("offset");
			if(Offset.is_array() && Offset.size() >= 2)
			{
				Group.m_OffsetX = static_cast<int>(Offset.at(0).as_int());
				Group.m_OffsetY = static_cast<int>(Offset.at(1).as_int());
			}
			if(Context.Has("clipping"))
				Group.m_UseClipping = Context.Bool("clipping") ? 1 : 0;
			const base::json &Clip = Context.Json("clip");
			if(Clip.is_array() && Clip.size() >= 4)
			{
				Group.m_ClipX = static_cast<int>(Clip.at(0).as_int());
				Group.m_ClipY = static_cast<int>(Clip.at(1).as_int());
				Group.m_ClipW = static_cast<int>(Clip.at(2).as_int());
				Group.m_ClipH = static_cast<int>(Clip.at(3).as_int());
			}
			Context.m_Editor.MarkDirty();
			return Context.Ok(Changed(Context.m_Editor, "groups.set"));
		}

		bool GroupsDelete(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("group", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vGroups.size()))
				return Context.Fail("groups.delete needs a valid \"group\"");
			Editor.PushUndoOpWhole(Index, 0);
			Editor.Map().m_vGroups.erase(Editor.Map().m_vGroups.begin() + Index);
			Editor.RefreshAfterStructuralEdit();
			return Context.Ok(Changed(Editor, "groups.delete"));
		}

		bool LayersList(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			base::json Result = base::json::object();
			base::json Layers = base::json::array();
			for(std::size_t g = 0; g < Editor.Map().m_vGroups.size(); g++)
				for(std::size_t l = 0; l < Editor.Map().m_vGroups[g].m_vLayers.size(); l++)
					Layers.push_back(LayerToJson(Editor, static_cast<int>(g), static_cast<int>(l), false));
			Result["layers"] = std::move(Layers);
			return Context.Ok(std::move(Result));
		}

		bool LayersGet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			if(FindLayer(Editor, Group, Layer, Error) == nullptr)
				return Context.Fail(Error);
			return Context.Ok(LayerToJson(Editor, Group, Layer, Context.Bool("tiles", false)));
		}

		bool LayersAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const std::string Kind = Context.Str("kind", "tiles");
			int Type = LAYERTYPE_TILES, Flags = 0;
			if(!FlagsFromKind(Kind, Type, Flags))
				return Context.Fail("unknown layer kind '" + Kind + "'; expected tiles, game, tele, speedup, front, switch, tune, quads or sounds");

			// the DDRace layers need either a DDRace target or the switch
			const bool bDdnetOnly = ((Flags & TILESLAYERFLAG_PHYSICS) != 0) || (Flags & TILESLAYERFLAG_GAME) != 0;
			if(bDdnetOnly && (Flags & TILESLAYERFLAG_PHYSICS) != 0 && !Editor.GameEntities())
				return Context.Fail("the DDRace physics layers need a DDRace target or allow_extensions: true");

			int Group = Context.Int("group", Editor.SelectedGroupIndex());
			if(Group < 0 || Group >= static_cast<int>(Editor.Map().m_vGroups.size()))
				return Context.Fail("layers.add needs a valid \"group\"");
			CGroup &Target = Editor.Map().m_vGroups[static_cast<std::size_t>(Group)];

			// a new layer matches the map's existing size unless told otherwise
			int Width = Context.Int("width", 0), Height = Context.Int("height", 0);
			if(Width <= 0 || Height <= 0)
				for(const CGroup &Other : Editor.Map().m_vGroups)
					for(const CLayer &Layer : Other.m_vLayers)
						if(Layer.IsTiles())
						{
							Width = Layer.m_Width;
							Height = Layer.m_Height;
						}
			if(Width <= 0 || Height <= 0)
			{
				Width = 50;
				Height = 50;
			}

			Editor.PushUndoOpWhole(Group, static_cast<int>(Target.m_vLayers.size()));
			CLayer *pLayer = nullptr;
			if(Type == LAYERTYPE_QUADS)
				pLayer = &Editor.Map().NewQuadLayer(Target, Context.Int("image", -1));
			else if(Type == LAYERTYPE_SOUNDS)
				pLayer = &Editor.Map().NewSoundLayer(Target, Context.Int("sound", -1));
			else
				pLayer = &Editor.Map().NewTileLayer(Target, Width, Height, Context.Int("image", -1));
			pLayer->m_Name = Context.Str("name", Kind);
			pLayer->m_Flags = Flags;
			pLayer->m_Color = CColor{255, 255, 255, 255};

			Editor.RefreshAfterStructuralEdit();
			base::json Result = Changed(Editor, "layers.add");
			Result["group"] = Group;
			Result["layer"] = static_cast<int>(Target.m_vLayers.size()) - 1;
			return Context.Ok(std::move(Result));
		}

		bool LayersSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			CLayer *pLayer = FindLayer(Editor, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			if(Context.Has("name"))
				pLayer->m_Name = Context.Str("name");
			if(Context.Has("image"))
				pLayer->m_Image = Context.Int("image");
			if(Context.Has("detail"))
				pLayer->m_LayerFlags = Context.Bool("detail") ? (pLayer->m_LayerFlags | LAYERFLAG_DETAIL) : (pLayer->m_LayerFlags & ~LAYERFLAG_DETAIL);
			if(Context.Has("flags"))
				pLayer->m_Flags = Context.Int("flags");
			if(Context.Has("pos_env"))
			{
				const int Envelope = Context.Int("pos_env", -1);
				if(Envelope >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
					return Context.Fail("pos_env does not name an envelope of this map");
				for(CQuad &Quad : pLayer->m_vQuads)
					Quad.m_PosEnv = Envelope;
			}
			if(Context.Has("color_env"))
			{
				const int Envelope = Context.Int("color_env", -1);
				if(Envelope >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
					return Context.Fail("color_env does not name an envelope of this map");
				pLayer->m_ColorEnv = Envelope;
			}
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "layers.set"));
		}

		bool LayersDelete(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			if(FindLayer(Editor, Group, Layer, Error) == nullptr)
				return Context.Fail(Error);
			Editor.PushUndoOpWhole(Group, Layer);
			CGroup &GroupRef = Editor.Map().m_vGroups[static_cast<std::size_t>(Group)];
			GroupRef.m_vLayers.erase(GroupRef.m_vLayers.begin() + Layer);
			Editor.RefreshAfterStructuralEdit();
			return Context.Ok(Changed(Editor, "layers.delete"));
		}

		bool LayersReorder(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int From = Context.Int("from", -1);
			const int To = Context.Int("to", -1);
			if(Group < 0 || Group >= static_cast<int>(Editor.Map().m_vGroups.size()))
				return Context.Fail("layers.reorder needs a valid \"group\"");
			CGroup &GroupRef = Editor.Map().m_vGroups[static_cast<std::size_t>(Group)];
			const int Count = static_cast<int>(GroupRef.m_vLayers.size());
			if(From < 0 || From >= Count || To < 0 || To >= Count)
				return Context.Fail("layers.reorder needs \"from\" and \"to\" inside the group");
			Editor.PushUndoOpWhole(Group, From);
			CLayer Moved = std::move(GroupRef.m_vLayers[static_cast<std::size_t>(From)]);
			GroupRef.m_vLayers.erase(GroupRef.m_vLayers.begin() + From);
			GroupRef.m_vLayers.insert(GroupRef.m_vLayers.begin() + To, std::move(Moved));
			Editor.RefreshAfterStructuralEdit();
			return Context.Ok(Changed(Editor, "layers.reorder"));
		}
	} // namespace

	// ------------------------------------------------------------------ tiles
	namespace
	{
		// the layer a tile method works on, from the parameters or the selection
		CLayer *TargetLayer(const CContext &Context, int &Group, int &Layer, std::string &Error)
		{
			Group = Context.Int("group", Context.m_Editor.SelectedGroupIndex());
			Layer = Context.Int("layer", Context.m_Editor.SelectedLayerIndex());
			return TileLayer(Context.m_Editor, Group, Layer, Error);
		}

		bool TilesGet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const int X = Context.Int("x", -1), Y = Context.Int("y", -1);
			if(X >= 0 && Y >= 0)
			{
				if(!pLayer->InBounds(X, Y))
					return Context.Fail("that tile is outside the layer");
				const CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(Y) * pLayer->m_Width + X];
				base::json Result = base::json::object();
				Result["index"] = static_cast<int>(Tile.m_Index);
				Result["flags"] = static_cast<int>(Tile.m_Flags);
				return Context.Ok(std::move(Result));
			}
			return Context.Ok(LayerToJson(Editor, Group, Layer, true));
		}

		bool TilesSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			const int X = Context.Int("x", -1), Y = Context.Int("y", -1);
			if(!pLayer->InBounds(X, Y))
				return Context.Fail("tiles.set needs an \"x\" and a \"y\" inside the layer");
			const int Index = Context.Int("index", 0);
			if(Index < 0 || Index > 255)
				return Context.Fail("a tile index is 0..255");
			const int Flags = FlagsFromJson(Context.Json("flags"));

			Editor.PushUndoOpTiles(Group, Layer);
			CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(Y) * pLayer->m_Width + X];
			Tile.m_Index = static_cast<std::uint8_t>(Index);
			// a game layer cell keeps its flags for the compiler, so the flags
			// are only meaningful on a plain tile layer
			if(!pLayer->IsGameLayer())
				Tile.m_Flags = static_cast<std::uint8_t>(Flags);
			Editor.MarkDirty();

			base::json Result = base::json::object();
			Result["index"] = static_cast<int>(Tile.m_Index);
			Result["flags"] = static_cast<int>(Tile.m_Flags);
			Result["group"] = Group;
			Result["layer"] = Layer;
			Result["x"] = X;
			Result["y"] = Y;
			return Context.Ok(std::move(Result));
		}

		bool TilesFill(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			int X0, Y0, X1, Y1;
			if(!RegionFromParams(Context, *pLayer, X0, Y0, X1, Y1, Error))
				return Context.Fail(Error);
			const int Index = Context.Int("index", 0);
			if(Index < 0 || Index > 255)
				return Context.Fail("a tile index is 0..255");
			const int Flags = FlagsFromJson(Context.Json("flags"));

			Editor.PushUndoOpTiles(Group, Layer);
			int Written = 0;
			for(int y = Y0; y <= Y1; y++)
				for(int x = X0; x <= X1; x++)
				{
					CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(y) * pLayer->m_Width + x];
					Tile.m_Index = static_cast<std::uint8_t>(Index);
					if(!pLayer->IsGameLayer())
						Tile.m_Flags = static_cast<std::uint8_t>(Flags);
					Written++;
				}
			Editor.MarkDirty();

			base::json Result = base::json::object();
			Result["written"] = Written;
			Result["region"] = base::json::array();
			Result["region"].push_back(X0);
			Result["region"].push_back(Y0);
			Result["region"].push_back(X1);
			Result["region"].push_back(Y1);
			return Context.Ok(std::move(Result));
		}

		/*
			A whole rectangle of tiles from the document's own tile encoding, so
			`tiles.get` and `tiles.set_region` are inverses: a caller can lift a
			region out of one layer and drop it into another.
		*/
		bool TilesSetRegion(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			const base::json &Tiles = Context.Json("tiles");
			if(!Tiles.is_array() || Tiles.size() == 0)
				return Context.Fail("tiles.set_region needs a \"tiles\" array");
			const int X0 = Context.Int("x", 0), Y0 = Context.Int("y", 0);
			const int Width = Context.Int("width", static_cast<int>(Tiles.size()));
			const int Height = Context.Int("height", Width > 0 ? static_cast<int>(Tiles.size()) / Width : 1);
			if(Width <= 0 || Height <= 0 || static_cast<std::size_t>(Width) * Height != Tiles.size())
				return Context.Fail("the tile count does not match \"width\" times \"height\"");

			Editor.PushUndoOpTiles(Group, Layer);
			int Written = 0;
			for(int y = 0; y < Height; y++)
				for(int x = 0; x < Width; x++)
				{
					if(!pLayer->InBounds(X0 + x, Y0 + y))
						continue;
					const base::json &Entry = Tiles.at(static_cast<std::size_t>(y) * Width + x);
					CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(Y0 + y) * pLayer->m_Width + (X0 + x)];
					if(Entry.is_object())
					{
						Tile.m_Index = static_cast<std::uint8_t>(base::clamp<int>(static_cast<int>(Entry["i"].as_int()), 0, 255));
						if(!pLayer->IsGameLayer())
							Tile.m_Flags = static_cast<std::uint8_t>(base::clamp<int>(static_cast<int>(Entry["f"].as_int()), 0, 255));
					}
					else
					{
						Tile.m_Index = static_cast<std::uint8_t>(base::clamp<int>(static_cast<int>(Entry.as_int()), 0, 255));
						if(!pLayer->IsGameLayer())
							Tile.m_Flags = 0;
					}
					Written++;
				}
			Editor.MarkDirty();
			base::json Result = base::json::object();
			Result["written"] = Written;
			return Context.Ok(std::move(Result));
		}

		bool TilesResize(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Width = Context.Int("width", 0), Height = Context.Int("height", 0);
			if(Width <= 0 || Height <= 0)
				return Context.Fail("tiles.resize needs a positive \"width\" and \"height\"");
			if(static_cast<long long>(Width) * Height > (1LL << 26))
				return Context.Fail("that is larger than the map format allows");
			const int Group = Context.Int("group", -1);
			const int Layer = Context.Int("layer", -1);
			Editor.PushUndoOpWhole(Group < 0 ? 0 : Group, Layer < 0 ? 0 : Layer);
			int Resized = 0;
			for(std::size_t g = 0; g < Editor.Map().m_vGroups.size(); g++)
				for(std::size_t l = 0; l < Editor.Map().m_vGroups[g].m_vLayers.size(); l++)
				{
					CLayer &LayerRef = Editor.Map().m_vGroups[g].m_vLayers[l];
					if(Group >= 0 && static_cast<int>(g) != Group)
						continue;
					if(Layer >= 0 && static_cast<int>(l) != Layer)
						continue;
					if(!LayerRef.IsTiles())
						continue;
					LayerRef.Resize(Width, Height);
					Resized++;
				}
			Editor.MarkDirty();
			base::json Result = base::json::object();
			Result["resized"] = Resized;
			Result["width"] = Width;
			Result["height"] = Height;
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ------------------------------------------------------------ game entities
	namespace
	{
		bool GameListEntities(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["entities"] = base::json::array();
			for(const CEntityName &Entry : EntityNames())
			{
				base::json Json = base::json::object();
				Json["index"] = Entry.m_Index;
				Json["name"] = Entry.m_pName;
				Json["ddnet_only"] = Entry.m_Index >= NUM_VANILLA_ENTITIES;
				Result["entities"].push_back(std::move(Json));
			}
			Result["allowed_in_this_target"] = Context.m_Editor.GameEntities();
			return Context.Ok(std::move(Result));
		}

		// resolves an entity parameter that may be a name or an index
		bool EntityFromParam(const base::json &Value, int &Out)
		{
			if(Value.is_number())
			{
				Out = static_cast<int>(Value.as_int());
				return true;
			}
			const std::string Name = base::ToLower(Value.as_string());
			if(Name.empty())
				return false;
			Out = EntityFromName(Name);
			return Out >= 0;
		}

		bool GameSetEntity(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = Context.Int("group", Editor.SelectedGroupIndex());
			int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			CLayer *pLayer = Editor.Layer(Group, Layer);
			if(pLayer == nullptr || !pLayer->IsGameLayer())
			{
				// the game layer is not always selected: find it
				bool bFound = false;
				for(std::size_t g = 0; g < Editor.Map().m_vGroups.size() && !bFound; g++)
					for(std::size_t l = 0; l < Editor.Map().m_vGroups[g].m_vLayers.size(); l++)
						if(Editor.Map().m_vGroups[g].m_vLayers[l].IsGameLayer())
						{
							Group = static_cast<int>(g);
							Layer = static_cast<int>(l);
							pLayer = &Editor.Map().m_vGroups[g].m_vLayers[l];
							bFound = true;
							break;
						}
				if(!bFound)
					return Context.Fail("this map has no game layer");
			}

			int Entity = ENTITY_NULL;
			if(!EntityFromParam(Context.Json("entity"), Entity))
				return Context.Fail("unknown entity; call game.list_entities for the names");
			if(Entity != ENTITY_NULL && Entity >= NUM_VANILLA_ENTITIES && !Editor.GameEntities())
				return Context.Fail(base::Format("entity {:d} is a DDRace only one: set allow_extensions to true or export to a DDRace target", Entity));

			const int X = Context.Int("x", -1), Y = Context.Int("y", -1);
			if(!pLayer->InBounds(X, Y))
				return Context.Fail("game.set_entity needs an \"x\" and a \"y\" inside the game layer");

			Editor.PushUndoOpTiles(Group, Layer);
			CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(Y) * pLayer->m_Width + X];
			// the entity offset is what makes a game layer cell an entity
			Tile.m_Index = static_cast<std::uint8_t>(Entity == ENTITY_NULL ? 0 : ENTITY_OFFSET + Entity);
			if(!Context.Has("keep_flags"))
				Tile.m_Flags = static_cast<std::uint8_t>(Context.Int("rotation", 0) & 0x3f);
			Editor.MarkDirty();

			base::json Result = base::json::object();
			Result["group"] = Group;
			Result["layer"] = Layer;
			Result["x"] = X;
			Result["y"] = Y;
			Result["entity"] = Entity;
			Result["raw"] = static_cast<int>(Tile.m_Index);
			return Context.Ok(std::move(Result));
		}

		bool GameListEntitiesInMap(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			base::json Entities = base::json::array();
			for(std::size_t g = 0; g < Editor.Map().m_vGroups.size(); g++)
				for(std::size_t l = 0; l < Editor.Map().m_vGroups[g].m_vLayers.size(); l++)
				{
					const CLayer &Layer = Editor.Map().m_vGroups[g].m_vLayers[l];
					if(!Layer.IsGameLayer())
						continue;
					for(int y = 0; y < Layer.m_Height; y++)
						for(int x = 0; x < Layer.m_Width; x++)
						{
							const CTile &Tile = Layer.m_vTiles[static_cast<std::size_t>(y) * Layer.m_Width + x];
							if(Tile.m_Index < ENTITY_OFFSET)
								continue;
							base::json Entry = base::json::object();
							Entry["group"] = static_cast<int>(g);
							Entry["layer"] = static_cast<int>(l);
							Entry["x"] = x;
							Entry["y"] = y;
							Entry["entity"] = Tile.m_Index - ENTITY_OFFSET;
							Entry["flags"] = static_cast<int>(Tile.m_Flags);
							Entities.push_back(std::move(Entry));
						}
				}
			base::json Result = base::json::object();
			Result["entities"] = std::move(Entities);
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// --------------------------------------------------------- physics payloads
	namespace
	{
		// the raw payload vector of a physics layer, by its flag
		byte_vector *PhysicsPayload(CLayer &Layer)
		{
			if(Layer.m_Flags & TILESLAYERFLAG_TELE) return &Layer.m_vTele;
			if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP) return &Layer.m_vSpeedup;
			if(Layer.m_Flags & TILESLAYERFLAG_FRONT) return &Layer.m_vFront;
			if(Layer.m_Flags & TILESLAYERFLAG_SWITCH) return &Layer.m_vSwitch;
			if(Layer.m_Flags & TILESLAYERFLAG_TUNE) return &Layer.m_vTune;
			return nullptr;
		}

		int PhysicsElementSize(const CLayer &Layer)
		{
			if(Layer.m_Flags & TILESLAYERFLAG_TELE) return 2;
			if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP) return 6;
			if(Layer.m_Flags & TILESLAYERFLAG_FRONT) return 4;
			if(Layer.m_Flags & TILESLAYERFLAG_SWITCH) return 4;
			if(Layer.m_Flags & TILESLAYERFLAG_TUNE) return 2;
			return 0;
		}

		bool PhysicsAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const std::string Kind = base::ToLower(Context.Str("kind"));
			int Flags = 0;
			if(Kind == "tele") Flags = TILESLAYERFLAG_TELE;
			else if(Kind == "speedup") Flags = TILESLAYERFLAG_SPEEDUP;
			else if(Kind == "front") Flags = TILESLAYERFLAG_FRONT;
			else if(Kind == "switch") Flags = TILESLAYERFLAG_SWITCH;
			else if(Kind == "tune") Flags = TILESLAYERFLAG_TUNE;
			else
				return Context.Fail("physics.add needs a \"kind\" of tele, speedup, front, switch or tune");
			Editor.AddPhysicsLayerOp(Flags);
			return Context.Ok(Changed(Editor, "physics.add"));
		}

		bool PhysicsGet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			CLayer *pLayer = TileLayer(Editor, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const byte_vector *pPayload = PhysicsPayload(*pLayer);
			if(pPayload == nullptr)
				return Context.Fail("that layer is not a DDRace physics layer");

			const int Element = PhysicsElementSize(*pLayer);
			base::json Result = base::json::object();
			Result["kind"] = LayerToJson(Editor, Group, Layer, false)["kind"];
			Result["element_size"] = Element;
			Result["count"] = static_cast<long long>(pPayload->size() / static_cast<std::size_t>(std::max(Element, 1)));

			const int X = Context.Int("x", -1), Y = Context.Int("y", -1);
			if(X >= 0 && Y >= 0)
			{
				if(!pLayer->InBounds(X, Y))
					return Context.Fail("that cell is outside the layer");
				const std::size_t At = (static_cast<std::size_t>(Y) * pLayer->m_Width + X) * static_cast<std::size_t>(Element);
				base::json Fields = base::json::array();
				for(int i = 0; i < Element && At + i < pPayload->size(); i++)
					Fields.push_back(std::to_integer<int>((*pPayload)[At + i]));
				Result["fields"] = std::move(Fields);
				return Context.Ok(std::move(Result));
			}

			// the whole payload, byte per byte, so an agent can lift and copy it
			base::json All = base::json::array();
			for(const std::byte Byte : *pPayload)
				All.push_back(std::to_integer<int>(Byte));
			Result["bytes"] = std::move(All);
			return Context.Ok(std::move(Result));
		}

		bool PhysicsSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			CLayer *pLayer = TileLayer(Editor, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			byte_vector *pPayload = PhysicsPayload(*pLayer);
			if(pPayload == nullptr)
				return Context.Fail("that layer is not a DDRace physics layer");
			const int Element = PhysicsElementSize(*pLayer);

			const int X = Context.Int("x", -1), Y = Context.Int("y", -1);
			if(!pLayer->InBounds(X, Y))
				return Context.Fail("physics.set needs an \"x\" and a \"y\" inside the layer");
			const base::json &Fields = Context.Json("fields");
			if(!Fields.is_array())
				return Context.Fail("physics.set needs a \"fields\" array");

			Editor.PushUndoOpTiles(Group, Layer);
			const std::size_t At = (static_cast<std::size_t>(Y) * pLayer->m_Width + X) * static_cast<std::size_t>(Element);
			for(std::size_t i = 0; i < Fields.size() && i < static_cast<std::size_t>(Element) && At + i < pPayload->size(); i++)
				(*pPayload)[At + i] = static_cast<std::byte>(base::clamp<int>(static_cast<int>(Fields.at(i).as_int()), 0, 255));
			// the drawn tile mirrors the payload's type byte, the way the loader
			// derives it, so the picture follows the data
			CTile &Tile = pLayer->m_vTiles[static_cast<std::size_t>(Y) * pLayer->m_Width + X];
			switch(pLayer->m_Flags)
			{
			case TILESLAYERFLAG_TELE: Tile.m_Index = std::to_integer<std::uint8_t>((*pPayload)[At + 1]); break;
			case TILESLAYERFLAG_SPEEDUP: Tile.m_Index = std::to_integer<std::uint8_t>((*pPayload)[At + 2]); break;
			case TILESLAYERFLAG_FRONT: Tile.m_Index = std::to_integer<std::uint8_t>((*pPayload)[At + 0]); break;
			case TILESLAYERFLAG_SWITCH: Tile.m_Index = std::to_integer<std::uint8_t>((*pPayload)[At + 1]); break;
			case TILESLAYERFLAG_TUNE: Tile.m_Index = std::to_integer<std::uint8_t>((*pPayload)[At + 1]); break;
			default: break;
			}
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "physics.set"));
		}
	} // namespace

	// --------------------------------------------------------------- envelopes
	namespace
	{
		bool EnvelopesList(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["envelopes"] = base::json::array();
			const CMap &Map = Context.m_Editor.Map();
			for(std::size_t i = 0; i < Map.m_vEnvelopes.size(); i++)
			{
				const CEnvelope &Envelope = Map.m_vEnvelopes[i];
				base::json Entry = base::json::object();
				Entry["index"] = static_cast<int>(i);
				Entry["name"] = Envelope.m_Name;
				Entry["channels"] = Envelope.m_Channels;
				Entry["points"] = static_cast<int>(Envelope.m_vPoints.size());
				Entry["bezier"] = Envelope.UsesBezier();
				Entry["length_ms"] = Envelope.m_vPoints.empty() ? 0 : Envelope.m_vPoints.back().m_Time;
				Result["envelopes"].push_back(std::move(Entry));
			}
			Result["colour_envelope_of_selection"] = Context.m_Editor.ColorEnvelopeOfSelection();
			Result["position_envelope_of_selection"] = Context.m_Editor.PositionEnvelopeOfSelection();
			return Context.Ok(std::move(Result));
		}

		bool EnvelopesGet(const CContext &Context)
		{
			const int Index = Context.Int("envelope", -1);
			const CMap &Map = Context.m_Editor.Map();
			if(Index < 0 || Index >= static_cast<int>(Map.m_vEnvelopes.size()))
				return Context.Fail("envelopes.get needs an \"envelope\" index");
			const CEnvelope &Envelope = Map.m_vEnvelopes[static_cast<std::size_t>(Index)];
			base::json Result = base::json::object();
			Result["index"] = Index;
			Result["name"] = Envelope.m_Name;
			Result["channels"] = Envelope.m_Channels;
			Result["synchronized"] = Envelope.m_Synchronized;
			Result["points"] = base::json::array();
			for(const CEnvelopePoint &Point : Envelope.m_vPoints)
			{
				base::json Entry = base::json::object();
				Entry["time"] = Point.m_Time;
				Entry["curve"] = Point.m_Curvetype;
				Entry["values"] = base::json::array();
				for(int c = 0; c < 4; c++)
					Entry["values"].push_back(Point.m_aValues[c]);
				Entry["in_tangent"] = base::json::array();
				Entry["out_tangent"] = base::json::array();
				for(int c = 0; c < 4; c++)
				{
					Entry["in_tangent"].push_back(base::json::array());
					Entry["in_tangent"].at(Entry["in_tangent"].size() - 1).push_back(Point.m_Bezier.m_aInTangentdx[c]);
					Entry["in_tangent"].at(Entry["in_tangent"].size() - 1).push_back(Point.m_Bezier.m_aInTangentdy[c]);
					Entry["out_tangent"].push_back(base::json::array());
					Entry["out_tangent"].at(Entry["out_tangent"].size() - 1).push_back(Point.m_Bezier.m_aOutTangentdx[c]);
					Entry["out_tangent"].at(Entry["out_tangent"].size() - 1).push_back(Point.m_Bezier.m_aOutTangentdy[c]);
				}
				Result["points"].push_back(std::move(Entry));
			}
			return Context.Ok(std::move(Result));
		}

		bool EnvelopesCreate(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Channels = base::clamp(Context.Int("channels", 4), 1, 4);
			Editor.PushUndoOpWhole(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());
			const int Index = Editor.AddEnvelopeOp(Channels);
			if(Context.Has("name"))
				Editor.Map().m_vEnvelopes[static_cast<std::size_t>(Index)].m_Name = Context.Str("name");
			base::json Result = Changed(Editor, "envelopes.create");
			Result["envelope"] = Index;
			return Context.Ok(std::move(Result));
		}

		bool EnvelopesSetPoint(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("envelope", -1);
			const int Point = Context.Int("point", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
				return Context.Fail("envelopes.set_point needs an \"envelope\" index");
			CEnvelope &Envelope = Editor.Map().m_vEnvelopes[static_cast<std::size_t>(Index)];
			if(Point < 0 || Point >= static_cast<int>(Envelope.m_vPoints.size()))
				return Context.Fail("envelopes.set_point needs a \"point\" index");

			Editor.PushUndoOpWhole(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());
			/*
				The point's address can change: writing a time sorts the points
				back into order, so the reference is taken again after the write
				rather than held across the sort.
			*/
			CEnvelopePoint Edited = Envelope.m_vPoints[static_cast<std::size_t>(Point)];
			CEnvelopePoint &Ref = Edited;
			if(Context.Has("time"))
				Ref.m_Time = std::max(Context.Int("time"), 0);
			if(Context.Has("curve"))
				Ref.m_Curvetype = base::clamp(Context.Int("curve"), 0, NUM_CURVETYPES - 1);
			const base::json &Values = Context.Json("values");
			if(Values.is_array())
				for(std::size_t c = 0; c < Values.size() && c < 4; c++)
					Ref.m_aValues[c] = static_cast<int>(Values.at(c).as_int());
			const base::json &InTangent = Context.Json("in_tangent");
			const base::json &OutTangent = Context.Json("out_tangent");
			for(int c = 0; c < 4; c++)
			{
				if(InTangent.is_array() && c < static_cast<int>(InTangent.size()) && InTangent.at(c).is_array() && InTangent.at(c).size() >= 2)
				{
					Ref.m_Bezier.m_aInTangentdx[c] = static_cast<int>(InTangent.at(c).at(0).as_int());
					Ref.m_Bezier.m_aInTangentdy[c] = static_cast<int>(InTangent.at(c).at(1).as_int());
				}
				if(OutTangent.is_array() && c < static_cast<int>(OutTangent.size()) && OutTangent.at(c).is_array() && OutTangent.at(c).size() >= 2)
				{
					Ref.m_Bezier.m_aOutTangentdx[c] = static_cast<int>(OutTangent.at(c).at(0).as_int());
					Ref.m_Bezier.m_aOutTangentdy[c] = static_cast<int>(OutTangent.at(c).at(1).as_int());
				}
			}
			// Writing a time is a move in the list, not a clamp: a caller that
			// says 1000 means "this point happens at 1000", and the points are
			// re-sorted so the evaluation stays well defined even when two of
			// them end up at the same time.
			Envelope.m_vPoints[static_cast<std::size_t>(Point)] = Edited;
			std::stable_sort(Envelope.m_vPoints.begin(), Envelope.m_vPoints.end(),
				[](const CEnvelopePoint &A, const CEnvelopePoint &B) { return A.m_Time < B.m_Time; });
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "envelopes.set_point");
			// report where it ended up, which is not necessarily where it was
			for(std::size_t i = 0; i < Envelope.m_vPoints.size(); i++)
				if(Envelope.m_vPoints[i].m_Time == Edited.m_Time && Envelope.m_vPoints[i].m_Curvetype == Edited.m_Curvetype &&
					std::memcmp(Envelope.m_vPoints[i].m_aValues, Edited.m_aValues, sizeof(Edited.m_aValues)) == 0)
				{
					Result["point"] = static_cast<int>(i);
					break;
				}
			return Context.Ok(std::move(Result));
		}

		bool EnvelopesAddPoint(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("envelope", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
				return Context.Fail("envelopes.add_point needs an \"envelope\" index");
			CEnvelope &Envelope = Editor.Map().m_vEnvelopes[static_cast<std::size_t>(Index)];
			Editor.PushUndoOpWhole(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());

			CEnvelopePoint Point;
			Point.m_Time = std::max(Context.Int("time", 0), 0);
			Point.m_Curvetype = base::clamp(Context.Int("curve", CURVETYPE_LINEAR), 0, NUM_CURVETYPES - 1);
			const base::json &Values = Context.Json("values");
			for(int c = 0; c < 4; c++)
				Point.m_aValues[c] = Values.is_array() && c < static_cast<int>(Values.size()) ? static_cast<int>(Values.at(c).as_int()) : 0;
			Envelope.m_vPoints.push_back(Point);
			std::sort(Envelope.m_vPoints.begin(), Envelope.m_vPoints.end(),
				[](const CEnvelopePoint &A, const CEnvelopePoint &B) { return A.m_Time < B.m_Time; });
			Editor.MarkDirty();

			// report where it landed after the sort
			int Where = 0;
			for(std::size_t i = 0; i < Envelope.m_vPoints.size(); i++)
				if(Envelope.m_vPoints[i].m_Time == Point.m_Time)
					Where = static_cast<int>(i);
			base::json Result = Changed(Editor, "envelopes.add_point");
			Result["point"] = Where;
			return Context.Ok(std::move(Result));
		}

		bool EnvelopesDeletePoint(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("envelope", -1);
			const int Point = Context.Int("point", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
				return Context.Fail("envelopes.delete_point needs an \"envelope\" index");
			CEnvelope &Envelope = Editor.Map().m_vEnvelopes[static_cast<std::size_t>(Index)];
			if(Point < 0 || Point >= static_cast<int>(Envelope.m_vPoints.size()))
				return Context.Fail("envelopes.delete_point needs a \"point\" index");
			if(Envelope.m_vPoints.size() <= 1)
				return Context.Fail("an envelope keeps its last point");
			Editor.PushUndoOpWhole(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());
			Envelope.m_vPoints.erase(Envelope.m_vPoints.begin() + Point);
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "envelopes.delete_point"));
		}

		bool EnvelopesSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("envelope", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
				return Context.Fail("envelopes.set needs an \"envelope\" index");
			CEnvelope &Envelope = Editor.Map().m_vEnvelopes[static_cast<std::size_t>(Index)];
			if(Context.Has("name"))
				Envelope.m_Name = Context.Str("name");
			if(Context.Has("channels"))
				Envelope.m_Channels = base::clamp(Context.Int("channels"), 1, 4);
			if(Context.Has("synchronized"))
				Envelope.m_Synchronized = Context.Bool("synchronized") ? 1 : 0;
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "envelopes.set"));
		}

		/*
			Attaching an envelope to the selection is what makes a curve visible
			on the map, so it is one method rather than three field writes.
		*/
		bool EnvelopesAttach(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Index = Context.Int("envelope", -1);
			if(Index < 0 || Index >= static_cast<int>(Editor.Map().m_vEnvelopes.size()))
				return Context.Fail("envelopes.attach needs an \"envelope\" index");
			const std::string Which = Context.Str("to", "colour");
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			std::string Error;
			CLayer *pLayer = FindLayer(Editor, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			if(pLayer->IsQuads() && (Which == "position" || Which == "pos"))
			{
				const int Quad = Context.Int("quad", Editor.SelectedQuads().empty() ? 0 : Editor.SelectedQuads().front());
				if(Quad < 0 || Quad >= static_cast<int>(pLayer->m_vQuads.size()))
					return Context.Fail("that quad does not exist");
				pLayer->m_vQuads[static_cast<std::size_t>(Quad)].m_PosEnv = Index;
			}
			else if(Which == "position" || Which == "pos")
			{
				return Context.Fail("only a quad has a position envelope");
			}
			else if(pLayer->IsQuads())
			{
				// every selected quad, or the first one, so the caller does not
				// have to loop
				const std::vector<int> &Selected = Editor.SelectedQuads();
				if(Selected.empty())
				{
					if(pLayer->m_vQuads.empty())
						return Context.Fail("that quad layer has no quads");
					pLayer->m_vQuads.front().m_ColorEnv = Index;
				}
				else
					for(const int Quad : Selected)
						if(Quad >= 0 && Quad < static_cast<int>(pLayer->m_vQuads.size()))
							pLayer->m_vQuads[static_cast<std::size_t>(Quad)].m_ColorEnv = Index;
			}
			else
			{
				pLayer->m_ColorEnv = Index;
				if(Which == "sound" || Which == "sound_level")
					pLayer->m_ColorEnvOffset = Context.Int("offset", pLayer->m_ColorEnvOffset);
			}
			Editor.MarkDirty();
			return Context.Ok(Changed(Editor, "envelopes.attach"));
		}
	} // namespace

	// ------------------------------------------------------------------ quads
	/*
		A quad is five points and four colours: the four corners of a shape and
		the pivot its position envelope turns it around, and one colour per
		corner, so a quad can be a gradient. The points are stored as 22.10
		fixed point, the coordinate unit a map uses; the interface speaks
		pixels, and QuadFixedFromUnits() is the one place the two meet.

		The corner order is the order the format stores rather than the order
		the outline is walked: 0 and 1 are the top edge and 2 and 3 the bottom
		one. A caller that does not think in those terms builds a quad with
		`at` and `size` and never has to.
	*/
	namespace
	{
		// the size of one tile in pixels, the step a quad is snapped to
		constexpr int QuadGridStep = 32;

		// the middle of the map's tiles, in pixels: where a quad goes when a
		// request does not say
		base::vec2 MapCentre(const CEditor &Editor)
		{
			for(const CGroup &Group : Editor.Map().m_vGroups)
				for(const CLayer &Layer : Group.m_vLayers)
					if(Layer.IsTiles() && Layer.m_Width > 0 && Layer.m_Height > 0)
						return base::vec2(Layer.m_Width * 16.0f, Layer.m_Height * 16.0f);
			return base::vec2(800.0f, 600.0f);
		}

		// a quad layer, or an error
		CLayer *QuadLayer(CEditor &Editor, int Group, int Layer, std::string &Error)
		{
			CLayer *pLayer = Editor.Layer(Group, Layer);
			if(pLayer == nullptr)
			{
				Error = base::Format("there is no layer {:d} of group {:d}", Layer, Group);
				return nullptr;
			}
			if(!pLayer->IsQuads())
			{
				Error = base::Format("layer {:d} of group {:d} is not a quad layer", Layer, Group);
				return nullptr;
			}
			return pLayer;
		}

		// the layer a quad method works on, from the parameters or the selection
		CLayer *TargetQuadLayer(const CContext &Context, int &Group, int &Layer, std::string &Error)
		{
			Group = Context.Int("group", Context.m_Editor.SelectedGroupIndex());
			Layer = Context.Int("layer", Context.m_Editor.SelectedLayerIndex());
			return QuadLayer(Context.m_Editor, Group, Layer, Error);
		}

		bool IsPoint(const base::json &Value) { return Value.is_array() && Value.size() >= 2; }

		base::vec2 PointFromJson(const base::json &Value, base::vec2 Fallback = base::vec2())
		{
			if(!IsPoint(Value))
				return Fallback;
			return base::vec2(static_cast<float>(Value.at(0).as_double()), static_cast<float>(Value.at(1).as_double()));
		}

		base::json PointToJson(CPoint Point)
		{
			base::json Result = base::json::array();
			Result.push_back(QuadUnitsFromFixed(Point.x));
			Result.push_back(QuadUnitsFromFixed(Point.y));
			return Result;
		}

		base::json ColorToJson(const CColor &Color)
		{
			base::json Result = base::json::array();
			Result.push_back(Color.r);
			Result.push_back(Color.g);
			Result.push_back(Color.b);
			Result.push_back(Color.a);
			return Result;
		}

		/*
			One colour is one key, so a quad's fill can be a single `color`
			for all four corners or a `colors` list with one each; both land on
			the same four stored entries.
		*/
		bool ColorFromJson(const base::json &Value, CColor &Color)
		{
			if(!Value.is_array() || Value.size() < 3)
				return false;
			Color.r = base::clamp(static_cast<int>(Value.at(0).as_int()), 0, 255);
			Color.g = base::clamp(static_cast<int>(Value.at(1).as_int()), 0, 255);
			Color.b = base::clamp(static_cast<int>(Value.at(2).as_int()), 0, 255);
			if(Value.size() >= 4)
				Color.a = base::clamp(static_cast<int>(Value.at(3).as_int()), 0, 255);
			return true;
		}

		// writes the colours the request names onto the quad; true when any was
		bool ApplyQuadColors(const CContext &Context, CQuad &Quad)
		{
			bool bTouched = false;
			CColor Color;
			/*
				`corner` is the picker's own granularity - one colour for the
				corner the right click landed on - so it narrows `color`
				rather than being applied after it: a request with both means
				"this colour on that corner", not "all four, then one again".
			*/
			const int Corner = Context.Int("corner", -1);
			const bool bOneCorner = Corner >= 0 && Corner < 4;
			if(ColorFromJson(Context.Json("color"), Color))
			{
				if(bOneCorner)
					Quad.m_aColors[Corner] = Color;
				else
					for(int i = 0; i < 4; i++)
						Quad.m_aColors[i] = Color;
				bTouched = true;
			}
			const base::json &Colors = Context.Json("colors");
			if(Colors.is_array())
				for(std::size_t i = 0; i < Colors.size() && i < 4; i++)
					if(ColorFromJson(Colors.at(i), Color))
					{
						Quad.m_aColors[i] = Color;
						bTouched = true;
					}
			return bTouched;
		}

		/*
			The quad itself, in the shape layers.get reports it, plus the two
			things a caller cannot compute from the stored points alone: the
			corners as the canvas draws them (through the position envelope) and
			the centroid the pivot is kept at.
		*/
		base::json QuadToJson(const CMap &Map, const CQuad &Quad, int Index, float Time)
		{
			base::json Result = base::json::object();
			Result["index"] = Index;
			Result["points"] = base::json::array();
			for(int i = 0; i < 5; i++)
				Result["points"].push_back(PointToJson(Quad.m_aPoints[i]));
			Result["colors"] = base::json::array();
			for(int i = 0; i < 4; i++)
				Result["colors"].push_back(ColorToJson(Quad.m_aColors[i]));
			Result["pos_env"] = Quad.m_PosEnv;
			Result["pos_env_offset"] = Quad.m_PosEnvOffset;
			Result["color_env"] = Quad.m_ColorEnv;
			Result["color_env_offset"] = Quad.m_ColorEnvOffset;

			base::vec2 aCorners[4];
			QuadPointsAt(Map, Quad, Time, aCorners);
			Result["corners"] = base::json::array();
			for(int i = 0; i < 4; i++)
			{
				base::json Point = base::json::array();
				Point.push_back(static_cast<double>(aCorners[i].x));
				Point.push_back(static_cast<double>(aCorners[i].y));
				Result["corners"].push_back(std::move(Point));
			}
			const base::vec2 Centre = QuadCentroid(Quad);
			Result["centroid"] = base::json::array();
			Result["centroid"].push_back(static_cast<double>(Centre.x));
			Result["centroid"].push_back(static_cast<double>(Centre.y));
			return Result;
		}

		// one or more quads of the layer, from "quad", "quads" or "all"
		bool QuadIndicesFromParams(const CContext &Context, int Count, std::vector<int> &Out, std::string &Error)
		{
			Out.clear();
			const base::json &List = Context.Json("quads");
			if(List.is_array())
			{
				for(std::size_t i = 0; i < List.size(); i++)
				{
					const int Index = static_cast<int>(List.at(i).as_int(-1));
					if(Index < 0 || Index >= Count)
					{
						Error = base::Format("quad {:d} does not exist; this layer has {:d}", Index, Count);
						return false;
					}
					if(std::find(Out.begin(), Out.end(), Index) == Out.end())
						Out.push_back(Index);
				}
				if(Out.empty())
				{
					Error = "\"quads\" is an empty list";
					return false;
				}
				return true;
			}
			if(Context.Bool("all", false))
			{
				if(Count <= 0)
				{
					Error = "this layer has no quads";
					return false;
				}
				Out.resize(static_cast<std::size_t>(Count));
				for(int i = 0; i < Count; i++)
					Out[static_cast<std::size_t>(i)] = i;
				return true;
			}
			const int Index = Context.Int("quad", -1);
			if(Index < 0 || Index >= Count)
			{
				Error = base::Format("quad {:d} does not exist; this layer has {:d}", Index, Count);
				return false;
			}
			Out.push_back(Index);
			return true;
		}

		/*
			The translation and rotation a quad's position envelope adds at a
			time. Every point edit is undone through it: the canvas shows the
			animated shape, so a script that reads a corner from quads.get and
			writes it straight back has to store the un-animated point or the
			quad drifts with every round trip.
		*/
		void QuadTransformAt(const CMap &Map, const CQuad &Quad, float Time, base::vec2 &Offset, float &Rotation)
		{
			CEnvelopeSample Position;
			Position[0] = Position[1] = Position[2] = 0.0f;
			if(Quad.m_PosEnv >= 0 && Quad.m_PosEnv < static_cast<int>(Map.m_vEnvelopes.size()))
				EvaluateEnvelope(Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_PosEnv)],
					Time + Quad.m_PosEnvOffset / 1000.0f, Position, 3);
			Offset = base::vec2(Position[0], Position[1]);
			Rotation = Position[2] / 180.0f * base::Pi;
		}

		// a point as the canvas shows it, to the fixed point the map stores
		CPoint QuadStoredPoint(const CMap &Map, const CQuad &Quad, float Time, base::vec2 Visible)
		{
			base::vec2 Offset;
			float Rotation = 0.0f;
			QuadTransformAt(Map, Quad, Time, Offset, Rotation);
			base::vec2 Target = Visible - Offset;
			if(Rotation != 0.0f)
			{
				const base::vec2 Pivot(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y));
				const base::vec2 Relative = Target - Pivot;
				const float Cos = std::cos(-Rotation);
				const float Sin = std::sin(-Rotation);
				Target = base::vec2(Relative.x * Cos - Relative.y * Sin, Relative.x * Sin + Relative.y * Cos) + Pivot;
			}
			CPoint Point;
			Point.x = QuadFixedFromUnits(Target.x);
			Point.y = QuadFixedFromUnits(Target.y);
			return Point;
		}

		// the envelope a request names, with null and -1 both meaning "none"
		bool EnvelopeFromParam(const CMap &Map, const base::json &Value, int &Out, std::string &Error)
		{
			if(Value.is_null())
			{
				Out = -1;
				return true;
			}
			const int Index = static_cast<int>(Value.as_int(-2));
			if(Index < -1 || Index >= static_cast<int>(Map.m_vEnvelopes.size()))
			{
				Error = base::Format("there is no envelope {:d}", Index);
				return false;
			}
			Out = Index;
			return true;
		}

		bool QuadsList(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			base::json Result = base::json::object();
			Result["group"] = Group;
			Result["layer"] = Layer;
			Result["image"] = pLayer->m_Image;
			Result["count"] = static_cast<int>(pLayer->m_vQuads.size());
			Result["selected"] = base::json::array();
			for(const int Quad : Editor.SelectedQuads())
				Result["selected"].push_back(Quad);
			// the bounding box of the ten points of every quad, which is how a
			// caller finds out where the quads it just asked about are
			Result["bounds"] = base::json::array();
			bool bFirst = true;
			double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
			for(const CQuad &Quad : pLayer->m_vQuads)
				for(int i = 0; i < 5; i++)
				{
					const double X = QuadUnitsFromFixed(Quad.m_aPoints[i].x);
					const double Y = QuadUnitsFromFixed(Quad.m_aPoints[i].y);
					MinX = bFirst ? X : std::min(MinX, X);
					MinY = bFirst ? Y : std::min(MinY, Y);
					MaxX = bFirst ? X : std::max(MaxX, X);
					MaxY = bFirst ? Y : std::max(MaxY, Y);
					bFirst = false;
				}
			if(!bFirst)
			{
				Result["bounds"].push_back(MinX);
				Result["bounds"].push_back(MinY);
				Result["bounds"].push_back(MaxX);
				Result["bounds"].push_back(MaxY);
			}
			base::json Quads = base::json::array();
			for(std::size_t i = 0; i < pLayer->m_vQuads.size(); i++)
				Quads.push_back(QuadToJson(Editor.Map(), pLayer->m_vQuads[i], static_cast<int>(i), Editor.EnvelopeTime()));
			Result["quads"] = std::move(Quads);
			return Context.Ok(std::move(Result));
		}

		bool QuadsGet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const int Index = Context.Int("quad", -1);
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				return Context.Fail(base::Format("quad {:d} does not exist; this layer has {:d}", Index,
					static_cast<int>(pLayer->m_vQuads.size())));
			base::json Result = QuadToJson(Editor.Map(), pLayer->m_vQuads[static_cast<std::size_t>(Index)], Index, Editor.EnvelopeTime());
			Result["group"] = Group;
			Result["layer"] = Layer;
			return Context.Ok(std::move(Result));
		}

		/*
			A new quad. There are two ways to say where it goes:

			  * `at` (a corner, or the middle with `center`), `size` and the
			    `color`/`colors` keys - a pixel rectangle turned into the four
			    stored corners and a pivot in the middle, which is what a caller
			    drawing a background or a panel wants;
			  * `points` and `colors` - a whole quad in the shape quads.get
			    reports, so a quad lifted from one layer can be dropped into
			    another unchanged.

			Without either, the quad is a 128 by 128 square in the middle of the
			map, so "add a quad" does something visible.
		*/
		bool QuadsAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);

			const base::json &SizeJson = Context.Json("size");
			base::vec2 Size = base::vec2(128.0f, 128.0f);
			if(IsPoint(SizeJson))
			{
				Size = PointFromJson(SizeJson);
				if(Size.x <= 0.0f || Size.y <= 0.0f)
					return Context.Fail("quads.add needs a positive \"size\"");
			}
			base::vec2 TopLeft = IsPoint(Context.Json("at")) ? PointFromJson(Context.Json("at")) : MapCentre(Editor);
			// a centre is a position, not a corner: the quad is moved so that
			// its middle lands there
			if(IsPoint(Context.Json("center")))
				TopLeft = PointFromJson(Context.Json("center")) - base::vec2(Size.x * 0.5f, Size.y * 0.5f);

			CQuad Quad;
			const base::json &Points = Context.Json("points");
			if(Points.is_array() && Points.size() >= 5)
			{
				for(int i = 0; i < 5; i++)
				{
					if(!IsPoint(Points.at(static_cast<std::size_t>(i))))
						return Context.Fail("\"points\" needs five [x, y] pairs: four corners and the pivot");
					Quad.m_aPoints[i].x = QuadFixedFromUnits(Points.at(static_cast<std::size_t>(i)).at(0).as_double());
					Quad.m_aPoints[i].y = QuadFixedFromUnits(Points.at(static_cast<std::size_t>(i)).at(1).as_double());
				}
			}
			else
			{
				base::vec2 aCorners[4];
				QuadGuessCorners(TopLeft, Size, aCorners);
				for(int i = 0; i < 4; i++)
				{
					Quad.m_aPoints[i].x = QuadFixedFromUnits(aCorners[i].x);
					Quad.m_aPoints[i].y = QuadFixedFromUnits(aCorners[i].y);
				}
				const base::vec2 Pivot = IsPoint(Context.Json("pivot")) ? PointFromJson(Context.Json("pivot")) :
					base::vec2(TopLeft.x + Size.x * 0.5f, TopLeft.y + Size.y * 0.5f);
				Quad.m_aPoints[4].x = QuadFixedFromUnits(Pivot.x);
				Quad.m_aPoints[4].y = QuadFixedFromUnits(Pivot.y);
			}

			// what is drawn without an image is the colours, so a quad with no
			// colour is white: a shape that can be seen and coloured later
			CColor Color{255, 255, 255, 255};
			ColorFromJson(Context.Json("color"), Color);
			for(int i = 0; i < 4; i++)
				Quad.m_aColors[i] = Color;
			ApplyQuadColors(Context, Quad);

			if(Context.Has("pos_env") && !EnvelopeFromParam(Editor.Map(), Context.Json("pos_env"), Quad.m_PosEnv, Error))
				return Context.Fail(Error);
			if(Context.Has("color_env") && !EnvelopeFromParam(Editor.Map(), Context.Json("color_env"), Quad.m_ColorEnv, Error))
				return Context.Fail(Error);
			Quad.m_PosEnvOffset = Context.Int("pos_env_offset", 0);
			Quad.m_ColorEnvOffset = Context.Int("color_env_offset", 0);

			/*
				The texture coordinates are what stretches the layer's image
				over the quad: 32 pixels of it by default, one tile, in the
				corner order the colours use.
			*/
			const base::json &Texcoords = Context.Json("texcoords");
			const float TexSize = static_cast<float>(Context.Json("tex_size").as_double(32.0));
			for(int i = 0; i < 4; i++)
			{
				const base::vec2 Tex = IsPoint(Texcoords) && Texcoords.size() >= 4 ?
					PointFromJson(Texcoords.at(static_cast<std::size_t>(i))) :
					base::vec2(CQuad::s_aCornerAt[i / 2][i % 2] * TexSize, (i / 2) * TexSize);
				Quad.m_aTexcoords[i].x = QuadFixedFromUnits(Tex.x);
				Quad.m_aTexcoords[i].y = QuadFixedFromUnits(Tex.y);
			}

			// `snap` is a step in pixels; SnapQuad() scales it to fixed point
			const int Snap = Context.Int("snap", 0);
			if(Snap > 0)
				SnapQuad(Editor.Map(), Quad, Snap);

			Editor.PushUndoOpWhole(Group, Layer);
			pLayer->m_vQuads.push_back(Quad);
			const int Added = static_cast<int>(pLayer->m_vQuads.size()) - 1;
			/*
				The new quad becomes the selection, because that is what makes it
				editable: the handles, the picker and the envelope panel all read
				the selection, so a quad an agent adds is the one the window
				highlights when the same editor is drawn. The layer comes with
				it, because a quad selection only means something inside the
				layer that holds the quads.
			*/
			Editor.SetSelectedLayer(Group, Layer);
			Editor.SelectQuad(Added);
			Editor.MarkDirty();

			base::json Result = Changed(Editor, "quads.add");
			Result["group"] = Group;
			Result["layer"] = Layer;
			Result["quad"] = Added;
			Result["count"] = static_cast<int>(pLayer->m_vQuads.size());
			Result["entry"] = QuadToJson(Editor.Map(), pLayer->m_vQuads.back(), Added, Editor.EnvelopeTime());
			return Context.Ok(std::move(Result));
		}

		/*
			Moving quads. `move` is a delta in pixels and applies to the whole
			quad including its pivot, so the shape keeps its place relative to
			the point it turns around; `at` moves the first corner there
			instead, keeping the quad's size and orientation, which is the way
			to put a quad somewhere exact.
		*/
		bool QuadsMove(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			const bool bAt = IsPoint(Context.Json("at"));
			const base::vec2 Delta = IsPoint(Context.Json("move")) ? PointFromJson(Context.Json("move")) : base::vec2();
			if(!bAt && Delta.x == 0.0f && Delta.y == 0.0f)
				return Context.Fail("quads.move needs a \"move\" [dx, dy] or an \"at\" [x, y]");
			// "at" is one place, so it names one quad: two quads cannot both put
			// their first corner on the same point and stay where they were
			if(bAt && Indices.size() > 1)
				return Context.Fail("quads.move with \"at\" takes one quad; use \"move\" for several");
			const int Snap = Context.Int("snap", 0);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
			{
				CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
				if(bAt)
				{
					/*
						The corner is the one the canvas shows, so a quad that a
						position envelope has moved is put where the caller can
						see it rather than where it is stored.
					*/
					base::vec2 aCorners[4];
					QuadPointsAt(Editor.Map(), Quad, Editor.EnvelopeTime(), aCorners);
					const base::vec2 Target = PointFromJson(Context.Json("at"));
					const int Fx = QuadFixedFromUnits(Target.x - aCorners[0].x);
					const int Fy = QuadFixedFromUnits(Target.y - aCorners[0].y);
					for(CPoint &Point : Quad.m_aPoints)
					{
						Point.x += Fx;
						Point.y += Fy;
					}
				}
				else
				{
					const int Fx = QuadFixedFromUnits(Delta.x);
					const int Fy = QuadFixedFromUnits(Delta.y);
					for(CPoint &Point : Quad.m_aPoints)
					{
						Point.x += Fx;
						Point.y += Fy;
					}
				}
				if(Snap > 0)
					SnapQuad(Editor.Map(), Quad, Snap);
			}
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.move");
			Result["moved"] = base::json::array();
			for(const int Index : Indices)
				Result["moved"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		/*
			One point where the canvas shows it. Point 4 is the pivot and points
			0 to 3 the corners, and the write is undone through the position
			envelope so that the point lands where the caller asked even when the
			quad is animated.
		*/
		bool QuadsSetPoint(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			// a corner may be named as a number or as the string the picker uses
			int Point = -1;
			if(Context.Str("point") == "pivot")
				Point = 4;
			else if(!Context.Has("point") || Context.Int("point", -1) < 0 || Context.Int("point", -1) > 4)
				return Context.Fail("quads.set_point needs a \"point\" from 0 to 4 (or \"pivot\")");
			else
				Point = Context.Int("point");
			const base::json &At = Context.Json("at");
			if(!IsPoint(At))
				return Context.Fail("quads.set_point needs an \"at\" [x, y]");
			const base::vec2 Visible = PointFromJson(At);
			const int Snap = Context.Int("snap", 0);

			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
			{
				CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
				// the pivot is stored as it is; a corner is stored without the
				// envelope's offset and rotation, which the canvas adds back
				CPoint Stored = Point == 4 ? CPoint{QuadFixedFromUnits(Visible.x), QuadFixedFromUnits(Visible.y)} :
					QuadStoredPoint(Editor.Map(), Quad, Editor.EnvelopeTime(), Visible);
				if(Snap > 0)
				{
					const int FixedStep = Snap * FixedPointScale;
					Stored.x = base::SnapToStep(Stored.x, FixedStep);
					Stored.y = base::SnapToStep(Stored.y, FixedStep);
				}
				Quad.m_aPoints[Point] = Stored;
			}
			Editor.MarkDirty();

			base::json Result = Changed(Editor, "quads.set_point");
			Result["point"] = Point;
			Result["at"] = PointToJson(pLayer->m_vQuads[static_cast<std::size_t>(Indices.front())].m_aPoints[Point]);
			Result["updated"] = base::json::array();
			for(const int Index : Indices)
				Result["updated"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		/*
			The whole shape of a quad at once: four corners in storage order and,
			optionally, the pivot. This is what reshaping a quad is - a freeform
			patch is four corners, not a rectangle - and the points are given as
			the canvas shows them.
		*/
		bool QuadsSetPoints(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const base::json &Points = Context.Json("points");
			if(!Points.is_array() || Points.size() < 4)
				return Context.Fail("quads.set_points needs a \"points\" array of four corners, and optionally the pivot");
			if(Points.size() > 5)
				return Context.Fail("a quad has four corners and one pivot");
			for(std::size_t i = 0; i < Points.size(); i++)
				if(!IsPoint(Points.at(i)))
					return Context.Fail("every entry of \"points\" is an [x, y] pair");

			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);
			const int Snap = Context.Int("snap", 0);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
			{
				CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
				for(std::size_t i = 0; i < Points.size(); i++)
				{
					const base::vec2 Visible = PointFromJson(Points.at(i));
					const CPoint Stored = i == 4 ? CPoint{QuadFixedFromUnits(Visible.x), QuadFixedFromUnits(Visible.y)} :
						QuadStoredPoint(Editor.Map(), Quad, Editor.EnvelopeTime(), Visible);
					Quad.m_aPoints[i] = Stored;
				}
				if(Snap > 0)
					SnapQuad(Editor.Map(), Quad, Snap);
			}
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.set_points");
			Result["updated"] = base::json::array();
			for(const int Index : Indices)
				Result["updated"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		// the four corner colours, or one of them
		bool QuadsSetColor(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			if(!Context.Has("color") && !Context.Has("colors"))
				return Context.Fail("quads.set_color needs a \"color\" [r, g, b, a] or a \"colors\" list");
			const int Corner = Context.Int("corner", -1);
			if(Context.Has("corner") && (Corner < 0 || Corner > 3))
				return Context.Fail("\"corner\" is 0 to 3; a state without it colours all four");

			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
				ApplyQuadColors(Context, pLayer->m_vQuads[static_cast<std::size_t>(Index)]);
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.set_color");
			Result["updated"] = base::json::array();
			for(const int Index : Indices)
				Result["updated"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		/*
			The layer's image and the texture coordinates together, because a
			quad that shows a picture needs both: the image names which picture,
			the coordinates say which part of it. `image` on the request is the
			layer's image, exactly as layers.set takes it.
		*/
		bool QuadsSetTexture(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			if(!Context.Has("image") && !Context.Has("texcoords") && !Context.Has("tex_size"))
				return Context.Fail("quads.set_texture needs an \"image\", \"texcoords\" or \"tex_size\"");

			const int Image = Context.Int("image", -1);
			if(Context.Has("image") && Image >= static_cast<int>(Editor.Map().m_vImages.size()))
				return Context.Fail(base::Format("there is no image {:d}", Image));

			Editor.PushUndoOpWhole(Group, Layer);
			if(Context.Has("image"))
				pLayer->m_Image = Image;

			const base::json &Texcoords = Context.Json("texcoords");
			if(Context.Has("tex_size") || (Texcoords.is_array() && Texcoords.size() >= 4))
			{
				std::vector<int> Indices;
				if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
					return Context.Fail(Error);
				const float TexSize = static_cast<float>(Context.Json("tex_size").as_double(32.0));
				for(const int Index : Indices)
				{
					CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
					for(int i = 0; i < 4; i++)
					{
						const base::vec2 Tex = Texcoords.is_array() && Texcoords.size() >= 4 ?
							PointFromJson(Texcoords.at(static_cast<std::size_t>(i))) :
							base::vec2(CQuad::s_aCornerAt[i / 2][i % 2] * TexSize, (i / 2) * TexSize);
						Quad.m_aTexcoords[i].x = QuadFixedFromUnits(Tex.x);
						Quad.m_aTexcoords[i].y = QuadFixedFromUnits(Tex.y);
					}
				}
			}
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.set_texture");
			Result["image"] = pLayer->m_Image;
			return Context.Ok(std::move(Result));
		}

		// the envelopes of one or more quads
		bool QuadsSetEnvelope(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			if(!Context.Has("pos_env") && !Context.Has("color_env"))
				return Context.Fail("quads.set_envelope needs a \"pos_env\" or a \"color_env\", -1 or null for none");

			int PosEnv = -1, ColorEnv = -1;
			if(Context.Has("pos_env") && !EnvelopeFromParam(Editor.Map(), Context.Json("pos_env"), PosEnv, Error))
				return Context.Fail(Error);
			if(Context.Has("color_env") && !EnvelopeFromParam(Editor.Map(), Context.Json("color_env"), ColorEnv, Error))
				return Context.Fail(Error);

			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
			{
				CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
				if(Context.Has("pos_env"))
				{
					Quad.m_PosEnv = PosEnv;
					if(Context.Has("pos_env_offset"))
						Quad.m_PosEnvOffset = Context.Int("pos_env_offset", 0);
				}
				if(Context.Has("color_env"))
				{
					Quad.m_ColorEnv = ColorEnv;
					if(Context.Has("color_env_offset"))
						Quad.m_ColorEnvOffset = Context.Int("color_env_offset", 0);
				}
			}
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.set_envelope");
			Result["updated"] = base::json::array();
			for(const int Index : Indices)
				Result["updated"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		/*
			Rounding a quad to a step, in both axes and for the texture
			coordinates with it. The reference editor does this with the tile
			grid when a quad is dropped, and it is what makes a hand drawn quad
			line up with the tiles around it.
		*/
		bool QuadsSnap(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const int Step = Context.Int("step", QuadGridStep);
			if(Step <= 0)
				return Context.Fail("quads.snap needs a positive \"step\"");
			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			for(const int Index : Indices)
				SnapQuad(Editor.Map(), pLayer->m_vQuads[static_cast<std::size_t>(Index)], Step);
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.snap");
			Result["step"] = Step;
			Result["updated"] = base::json::array();
			for(const int Index : Indices)
				Result["updated"].push_back(Index);
			return Context.Ok(std::move(Result));
		}

		// copies quads into the same layer or another one, at the end, so the
		// copies are the quads with the highest indices
		bool QuadsDuplicate(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			// the target layer defaults to the source one
			int IntoGroup = Group, IntoLayer = Layer;
			CLayer *pInto = pLayer;
			if(Context.Has("to_group") || Context.Has("to_layer"))
			{
				IntoGroup = Context.Int("to_group", Group);
				IntoLayer = Context.Int("to_layer", Layer);
				pInto = QuadLayer(Editor, IntoGroup, IntoLayer, Error);
				if(pInto == nullptr)
					return Context.Fail(Error);
			}
			const base::vec2 Delta = IsPoint(Context.Json("move")) ? PointFromJson(Context.Json("move")) : base::vec2();

			Editor.PushUndoOpWhole(IntoGroup, IntoLayer);
			base::json Copies = base::json::array();
			for(const int Index : Indices)
			{
				CQuad Copy = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
				for(CPoint &Point : Copy.m_aPoints)
				{
					Point.x += QuadFixedFromUnits(Delta.x);
					Point.y += QuadFixedFromUnits(Delta.y);
				}
				pInto->m_vQuads.push_back(Copy);
				Copies.push_back(static_cast<int>(pInto->m_vQuads.size()) - 1);
			}
			// the copies become the selection when they landed in the layer
			// that is being edited; a copy into another layer leaves this one
			// selected, because the selection belongs to a single layer
			if(pInto == pLayer && Editor.SelectedGroupIndex() == Group && Editor.SelectedLayerIndex() == Layer)
			{
				Editor.ClearQuadSelection();
				for(std::size_t i = 0; i < Copies.size(); i++)
					Editor.AddQuadSelection(static_cast<int>(Copies.at(i).as_int()));
			}
			Editor.MarkDirty();
			base::json Result = Changed(Editor, "quads.duplicate");
			Result["group"] = IntoGroup;
			Result["layer"] = IntoLayer;
			Result["quads"] = std::move(Copies);
			Result["count"] = static_cast<int>(pInto->m_vQuads.size());
			return Context.Ok(std::move(Result));
		}

		bool QuadsDelete(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			std::vector<int> Indices;
			if(!QuadIndicesFromParams(Context, static_cast<int>(pLayer->m_vQuads.size()), Indices, Error))
				return Context.Fail(Error);

			Editor.PushUndoOpWhole(Group, Layer);
			// highest first, so removing one does not move the next
			std::sort(Indices.begin(), Indices.end(), std::greater<int>());
			for(const int Index : Indices)
				pLayer->m_vQuads.erase(pLayer->m_vQuads.begin() + Index);
			/*
				The selection is a list of indices into the layer, so every one
				above a removed quad moves down with it. ClampSelection() would
				prune the ones that fell off the end; the shift is what keeps the
				rest selected.
			*/
			std::vector<int> Remaining;
			for(const int Selected : Editor.SelectedQuads())
			{
				int Shifted = Selected;
				for(const int Index : Indices)
					if(Selected > Index)
						Shifted--;
				if(Shifted >= 0 && Shifted < static_cast<int>(pLayer->m_vQuads.size()) &&
					std::find(Remaining.begin(), Remaining.end(), Shifted) == Remaining.end())
					Remaining.push_back(Shifted);
			}
			Editor.ClearQuadSelection();
			for(const int Index : Remaining)
				Editor.AddQuadSelection(Index);
			Editor.MarkDirty();

			base::json Result = Changed(Editor, "quads.delete");
			Result["deleted"] = static_cast<int>(Indices.size());
			Result["deleted_indices"] = base::json::array();
			for(const int Index : Indices)
				Result["deleted_indices"].push_back(Index);
			Result["count"] = static_cast<int>(pLayer->m_vQuads.size());
			return Context.Ok(std::move(Result));
		}

		/*
			Finding a quad. `at` is a point in map pixels and the test is the
			one the canvas does - against the shape as the envelope clock shows
			it - so "which quad is at this pixel" is answerable without reading
			the whole layer back.
		*/
		bool QuadsPick(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetQuadLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			const base::json &At = Context.Json("at");
			if(!IsPoint(At))
				return Context.Fail("quads.pick needs an \"at\" [x, y]");
			const float Time = static_cast<float>(Context.Json("time").as_double(Editor.EnvelopeTime()));
			const int Index = QuadIndexAt(Editor.Map(), *pLayer, Time, PointFromJson(At));

			base::json Result = base::json::object();
			Result["group"] = Group;
			Result["layer"] = Layer;
			Result["quad"] = Index;
			Result["found"] = Index >= 0;
			Result["time"] = static_cast<double>(Time);
			if(Index >= 0 && Context.Bool("select", true))
			{
				Editor.SetSelectedLayer(Group, Layer);
				Editor.SelectQuad(Index);
				Result["selection"] = SelectionToJson(Editor);
			}
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ------------------------------------------------------------------ rules
	namespace
	{
		bool RulesGet(const CContext &Context)
		{
			return Context.Ok(rules::ToJson(Context.m_Editor.RuleSet()));
		}

		bool RulesSet(const CContext &Context)
		{
			std::string Error;
			const rules::CRuleSet Parsed = rules::FromJson(Context.m_Params, &Error);
			if(!Error.empty())
				return Context.Fail(Error);
			Context.m_Editor.RuleSet() = Parsed;
			Context.m_Editor.MarkDirty();
			base::json Result = Changed(Context.m_Editor, "rules.set");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		bool RulesNew(const CContext &Context)
		{
			Context.m_Editor.NewRuleSetOp();
			if(Context.Has("name"))
				Context.m_Editor.RuleSet().m_Name = Context.Str("name");
			base::json Result = Changed(Context.m_Editor, "rules.new");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		bool RulesAddConfig(const CContext &Context)
		{
			Context.m_Editor.AddRuleConfigOp();
			if(Context.Has("name"))
				Context.m_Editor.RuleSet().m_vConfigs.back().m_Name = Context.Str("name");
			base::json Result = Changed(Context.m_Editor, "rules.add_config");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		bool RulesAddRun(const CContext &Context)
		{
			Context.m_Editor.SelectRule(Context.Int("config", 0), 0, 0);
			Context.m_Editor.AddRuleRunOp();
			base::json Result = Changed(Context.m_Editor, "rules.add_run");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		bool RulesAddRule(const CContext &Context)
		{
			Context.m_Editor.SelectRule(Context.Int("config", 0), Context.Int("run", 0), 0);
			// a rule that writes the given tile, with the given flags
			const int Tile = Context.Int("tile", -1);
			const int Flags = FlagsFromJson(Context.Json("flags"));
			Context.m_Editor.AddRuleOp();
			/*
				The rule goes into the run that was selected, not always into the
				first one: the run parameter names its target, so patching run 0
				would edit a different run than the caller asked for - and read
				the back() of a run that may be empty.
			*/
			const int ConfigIndex = Context.m_Editor.RuleConfig();
			const int RunIndex = Context.m_Editor.RuleRun();
			if(ConfigIndex < 0 || ConfigIndex >= static_cast<int>(Context.m_Editor.RuleSet().m_vConfigs.size()))
				return Context.Fail("rules.add_rule has no configuration to add to");
			rules::CConfiguration &Config = Context.m_Editor.RuleSet().m_vConfigs[static_cast<std::size_t>(ConfigIndex)];
			if(RunIndex < 0 || RunIndex >= static_cast<int>(Config.m_vRuns.size()))
				return Context.Fail(base::Format("run {:d} is not in configuration {:d}", RunIndex, ConfigIndex));
			rules::CRun &Run = Config.m_vRuns[static_cast<std::size_t>(RunIndex)];
			if(Run.m_vIndexRules.empty())
				return Context.Fail("the rule was not added to the run");
			rules::CIndexRule &Rule = Run.m_vIndexRules.back();
			if(Tile >= 0)
				Rule.m_Tile = Tile;
			Rule.m_Flag = Flags;
			if(Context.Has("name"))
				Rule.m_Name = Context.Str("name");
			if(Context.Has("random"))
				Rule.m_RandomProbability = static_cast<float>(Context.Json("random").as_double(1.0));
			if(Context.Has("skip_empty"))
				Rule.m_bSkipEmpty = Context.Bool("skip_empty");
			if(Context.Has("skip_full"))
				Rule.m_bSkipFull = Context.Bool("skip_full");

			// and the position rules of the same call, so a conditional rule is
			// one request rather than a request per condition
			const base::json &When = Context.Json("when");
			if(When.is_array())
				for(std::size_t i = 0; i < When.size(); i++)
				{
					const base::json &Entry = When.at(i);
					rules::CPosRule PosRule;
					const base::json &At = Entry["at"];
					if(At.is_array() && At.size() >= 2)
					{
						PosRule.m_X = static_cast<int>(At.at(0).as_int());
						PosRule.m_Y = static_cast<int>(At.at(1).as_int());
					}
					const base::json &Is = Entry["is"];
					const base::json &Not = Entry["not"];
					const base::json &List = Is.is_array() ? Is : Not;
					if(List.is_array())
					{
						PosRule.m_Mode = Is.is_array() ? rules::pos_rule_mode::index : rules::pos_rule_mode::not_index;
						for(std::size_t k = 0; k < List.size(); k++)
						{
							rules::CIndexInfo Info;
							const base::json &Value = List.at(k);
							if(Value.is_string())
							{
								const rules::tile_role Role = rules::RoleFromName(Value.as_string());
								Info.m_Role = Role == rules::tile_role::custom ? -1 : static_cast<int>(Role);
								Info.m_Tile = rules::RoleTileIndex(Role);
							}
							else
							{
								Info.m_Tile = static_cast<int>(Value.as_int());
							}
							PosRule.m_vIndices.push_back(Info);
						}
					}
					Rule.m_vRules.push_back(std::move(PosRule));
				}

			const base::json &Modulo = Context.Json("modulo");
			if(Modulo.is_array())
				for(std::size_t i = 0; i < Modulo.size(); i++)
				{
					const base::json &Entry = Modulo.at(i);
					rules::CModuloRule Mod;
					const base::json &Every = Entry["every"];
					const base::json &Offset = Entry["offset"];
					if(Every.is_array() && Every.size() >= 2)
					{
						Mod.m_ModX = std::max<int>(static_cast<int>(Every.at(0).as_int(1)), 1);
						Mod.m_ModY = std::max<int>(static_cast<int>(Every.at(1).as_int(1)), 1);
					}
					if(Offset.is_array() && Offset.size() >= 2)
					{
						Mod.m_OffsetX = static_cast<int>(Offset.at(0).as_int());
						Mod.m_OffsetY = static_cast<int>(Offset.at(1).as_int());
					}
					Rule.m_vModuloRules.push_back(Mod);
				}

			Context.m_Editor.MarkDirty();
			base::json Result = Changed(Context.m_Editor, "rules.add_rule");
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		/*
			Delete at a level: a whole configuration, a run, or one index rule.
			The level is what the tree rows mean when they are clicked, so the
			method and the button remove the same thing.
		*/
		bool RulesDelete(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const std::string What = Context.Str("level", "rule");
			const int Config = Context.Int("config", Editor.RuleConfig());
			const int Run = Context.Int("run", Editor.RuleRun());
			const int Rule = Context.Int("rule", Editor.RuleIndexInRun());
			const int Level = What == "config" ? 0 : (What == "run" ? 1 : 2);
			Editor.SelectRule(Config, Run, Rule, Level);
			if(!Editor.CanDeleteRuleSelection())
				return Context.Fail("there is nothing to delete at that level");
			Editor.DeleteRuleSelectionOp();
			base::json Result = Changed(Editor, "rules.delete");
			Result["level"] = What;
			Result["rules"] = rules::ToJson(Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}

		bool RulesApply(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const bool bAll = Context.Bool("all", false);
			const int ConfigIndex = bAll ? -1 : Context.Int("config", Editor.RuleConfig());
			int Slot = Context.Int("target", -1);
			if(Slot < 0)
				Slot = Editor.SlotOfLayer(Context.Int("group", Editor.SelectedGroupIndex()),
					Context.Int("layer", Editor.SelectedLayerIndex()));
			if(Slot < 0)
				return Context.Fail("rules.apply needs a tile layer: pass \"target\" as an engine slot, or select one");

			std::string Error;
			if(!Editor.ApplyRules(ConfigIndex, Slot, Context.Int("seed", 0), &Error))
				return Context.Fail(Error);

			base::json Result = Changed(Editor, "rules.apply");
			Result["target_slot"] = Slot;
			// the seed that was really used, so a random run is reproducible
			Result["seed"] = Editor.LastRuleSeed();
			Result["status"] = Editor.Status();
			return Context.Ok(std::move(Result));
		}

		bool RulesExport(const CContext &Context)
		{
			std::string Path = Context.Str("path");
			const std::string Format = Context.Str("format", "json");
			if(Path.empty())
			{
				const std::string Name = Context.m_Editor.ProjectName().empty() ? "rules" : Context.m_Editor.ProjectName();
				Path = (storage::GetProjectsDirectory() / (storage::SanitiseName(Name) + (Format == "ddnet" ? ".rules" : ".rules.json"))).string();
			}
			if(Format == "ddnet" || Format == "text")
				return Context.Fail("writing the DDRace .rules text format is not supported yet; the JSON format is the editable one and rules.import reads a .rules file");
			if(!Context.m_Editor.SaveRules(Path))
				return Context.Fail(Context.m_Editor.Status());
			base::json Result = base::json::object();
			Result["path"] = Path;
			Result["rules"] = rules::ToJson(Context.m_Editor.RuleSet());
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ------------------------------------------------------------ live tiles
	namespace
	{
		base::json LiveToJson(const CLayer &Layer, int Group, int LayerIndex)
		{
			base::json Result = base::json::object();
			Result["group"] = Group;
			Result["layer"] = LayerIndex;
			Result["enabled"] = Layer.m_bLive;
			Result["source_group"] = Layer.m_LiveSourceGroup;
			Result["source_layer"] = Layer.m_LiveSourceLayer;
			base::json Maps = base::json::array();
			for(const CLiveMap &Map : Layer.m_vLiveMaps)
			{
				base::json Entry = base::json::object();
				Entry["source"] = Map.m_Source;
				Entry["target"] = Map.m_Target;
				Entry["flags"] = Map.m_Flags;
				Maps.push_back(std::move(Entry));
			}
			Result["maps"] = std::move(Maps);
			return Result;
		}

		bool LiveGet(const CContext &Context)
		{
			int Group = 0, Layer = 0;
			std::string Error;
			const CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			return Context.Ok(LiveToJson(*pLayer, Group, Layer));
		}

		/*
			The live tiles are a property of one layer, so this sets the whole
			configuration in one call and runs the pass: enabled false turns it
			off, and maps is the source-to-output table the panel edits.
		*/
		bool LiveSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Group = 0, Layer = 0;
			std::string Error;
			CLayer *pLayer = TargetLayer(Context, Group, Layer, Error);
			if(pLayer == nullptr)
				return Context.Fail(Error);
			if(!pLayer->IsTiles())
				return Context.Fail("live tiles need a tile layer");

			Editor.PushUndoOpTiles(Group, Layer);
			pLayer->m_bLive = Context.Bool("enabled", true);
			pLayer->m_LiveSourceGroup = Context.Int("source_group", -1);
			pLayer->m_LiveSourceLayer = Context.Int("source_layer", -1);
			pLayer->m_vLiveMaps.clear();
			const base::json &Maps = Context.Json("maps");
			if(Maps.is_array())
				for(std::size_t i = 0; i < Maps.size(); i++)
				{
					const base::json &Entry = Maps.at(i);
					if(!Entry.is_object())
						continue;
					CLiveMap Map;
					Map.m_Source = base::clamp<int>(static_cast<int>(Entry["source"].as_int(0)), 0, 255);
					Map.m_Target = base::clamp<int>(static_cast<int>(Entry["target"].as_int(0)), 0, 255);
					Map.m_Flags = FlagsFromJson(Entry["flags"]);
					pLayer->m_vLiveMaps.push_back(Map);
				}
			Editor.MarkDirty();
			// the pass writes the dependents of the chosen source, this layer
			// among them
			if(pLayer->m_bLive && pLayer->m_LiveSourceGroup >= 0 && pLayer->m_LiveSourceLayer >= 0)
				Editor.ApplyLiveTiles(pLayer->m_LiveSourceGroup, pLayer->m_LiveSourceLayer);

			const CLayer *pStored = Editor.Layer(Group, Layer);
			base::json Result = Changed(Editor, "live.set");
			if(pStored != nullptr)
				Result["live"] = LiveToJson(*pStored, Group, Layer);
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ---------------------------------------------------------------- export
	namespace
	{
		/*
			A one-shot invocation starts from an empty editor, so an export that
			is told which project to write would otherwise export an empty
			untitled map. Opening it here is the same convenience
			export.package has, and it is what makes `uniteed export.map
			--project demo` do what it says.
		*/
		bool SeedFromProject(const CContext &Context)
		{
			const std::string Project = Context.Str("project");
			if(Project.empty() || Context.m_Editor.SessionSeeded())
				return true;
			if(!Context.m_Editor.LoadProject(ResolveProjectPath(Project).string()))
				return Context.Fail(Context.m_Editor.Status());
			return true;
		}

		bool ExportMap(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!SeedFromProject(Context))
				return false;
			std::string Error;
			map_target Target = Editor.Target();
			if(Context.Has("target") && !TargetFromParam(Context.Json("target"), Target, &Error))
				return Context.Fail(Error);

			std::string Path = Context.Str("path");
			if(Path.empty())
			{
				const std::string Name = Editor.ProjectName().empty() ? "untitled" : Editor.ProjectName();
				Path = storage::ExportPath(GetTargetProfile(Target).m_pId, Name).string();
			}
			if(!Editor.ExportMap(Path, Target))
				return Context.Fail(Editor.Status());

			base::json Result = base::json::object();
			Result["path"] = Path;
			Result["target"] = std::string(GetTargetProfile(Target).m_pId);
			Result["profile"] = GetTargetProfile(Target).m_pName;
			Result["datafile_version"] = GetTargetProfile(Target).m_DatafileVersion;
			Result["status"] = Editor.Status();
			Result["warnings"] = base::json::array();
			for(const std::string &Warning : Editor.Map().CompatibilityWarnings(Target))
				Result["warnings"].push_back(Warning);
			return Context.Ok(std::move(Result));
		}

		bool ExportAll(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!SeedFromProject(Context))
				return false;
			base::json Result = base::json::object();
			Result["exports"] = base::json::array();
			for(const map_target Target : AllTargets())
			{
				if(Context.Has("targets"))
				{
					bool bWanted = false;
					const base::json &Wanted = Context.Json("targets");
					for(std::size_t i = 0; i < Wanted.size(); i++)
						if(base::ToLower(Wanted.at(i).as_string()) == GetTargetProfile(Target).m_pId)
							bWanted = true;
					if(!bWanted)
						continue;
				}
				const std::string Name = Editor.ProjectName().empty() ? "untitled" : Editor.ProjectName();
				const std::string Path = storage::ExportPath(GetTargetProfile(Target).m_pId, Name).string();
				base::json Entry = base::json::object();
				Entry["target"] = std::string(GetTargetProfile(Target).m_pId);
				Entry["path"] = Path;
				if(Editor.ExportMap(Path, Target))
					Entry["ok"] = true;
				else
				{
					Entry["ok"] = false;
					Entry["error"] = Editor.Status();
				}
				Result["exports"].push_back(std::move(Entry));
			}
			return Context.Ok(std::move(Result));
		}

		bool ExportPackage(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			/*
				A one-shot invocation starts from an empty editor, so without this
				`export.package` would faithfully pack an empty untitled map. The
				`project` parameter names the project to open first, which is also
				the answer to "pack the map I was working on".
			*/
			if(!SeedFromProject(Context))
				return false;
			std::string Path = Context.Str("path");
			if(Path.empty())
			{
				// an export, so it lands in the export directory of the target
				// the mapper is working towards, not in the projects directory
				const std::string Name = Editor.ProjectName().empty() ? "untitled" : Editor.ProjectName();
				Path = storage::PackageExportPath(GetTargetProfile(Editor.Target()).m_pId, Name).string();
			}
			if(!Editor.SavePackage(Path))
				return Context.Fail(Editor.Status());
			base::json Result = base::json::object();
			Result["path"] = Path;
			Result["status"] = Editor.Status();
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// --------------------------------------------------------------- selection
	namespace
	{
		bool SelectSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const int Group = Context.Int("group", Editor.SelectedGroupIndex());
			const int Layer = Context.Int("layer", Editor.SelectedLayerIndex());
			Editor.SetSelectedLayer(Group, Layer);
			// the extensions gate is part of the selection state an agent wants
			// to read back, so it is refreshed with it
			base::json Result = Changed(Editor, "select.set");
			Result["selection"] = SelectionToJson(Editor);
			return Context.Ok(std::move(Result));
		}

		bool SelectGet(const CContext &Context)
		{
			return Context.Ok(SelectionToJson(Context.m_Editor));
		}

		bool SelectBrush(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(Context.Has("tile"))
				Editor.SetBrushTile(Context.Int("tile"));
			if(Context.Has("size"))
				Editor.SetBrushSize(Context.Int("size", 1));
			if(Context.Has("flags"))
				Editor.SetBrushFlags(FlagsFromJson(Context.Json("flags")));
			return Context.Ok(Changed(Editor, "select.brush"));
		}

		bool SelectQuad(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const base::json &Quads = Context.Json("quads");
			if(Quads.is_array())
			{
				Editor.ClearQuadSelection();
				for(std::size_t i = 0; i < Quads.size(); i++)
					Editor.AddQuadSelection(static_cast<int>(Quads.at(i).as_int()));
			}
			else if(Context.Has("quad"))
			{
				Editor.SelectQuad(Context.Int("quad"));
			}
			base::json Result = Changed(Editor, "select.quad");
			Result["selection"] = SelectionToJson(Editor);
			return Context.Ok(std::move(Result));
		}

		bool SelectView(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(Context.Has("zoom"))
				Editor.SetZoom(static_cast<float>(Context.Json("zoom").as_double(1.0)));
			if(Context.Has("center"))
			{
				const base::json &Center = Context.Json("center");
				if(Center.is_array() && Center.size() >= 2)
					Editor.SetCenter(base::vec2(static_cast<float>(Center.at(0).as_double()), static_cast<float>(Center.at(1).as_double())));
			}
			if(Context.Has("grid"))
				Editor.SetGrid(Context.Bool("grid"));
			if(Context.Bool("fit", false))
				Editor.FitToMap();
			return Context.Ok(Changed(Editor, "select.view"));
		}
	} // namespace

	// ----------------------------------------------------------------- history
	namespace
	{
		// the same compact base64 the JSON sidecars use, for a caller that wants
		// to carry an image or a sound in the request itself
		int Base64Value(char Character)
		{
			if(Character >= 'A' && Character <= 'Z') return Character - 'A';
			if(Character >= 'a' && Character <= 'z') return Character - 'a' + 26;
			if(Character >= '0' && Character <= '9') return Character - '0' + 52;
			if(Character == '+') return 62;
			if(Character == '/') return 63;
			return -1;
		}

		bool DecodeBase64(std::string_view Text, byte_vector &Out)
		{
			Out.clear();
			unsigned Buffer = 0;
			int Bits = 0;
			for(const char Character : Text)
			{
				if(Character == '=' || Character == '\n' || Character == '\r' || Character == ' ')
					continue;
				const int Value = Base64Value(Character);
				if(Value < 0)
					return false;
				Buffer = (Buffer << 6) | static_cast<unsigned>(Value);
				Bits += 6;
				if(Bits >= 8)
				{
					Bits -= 8;
					Out.push_back(static_cast<std::byte>((Buffer >> Bits) & 0xff));
				}
			}
			return true;
		}

		bool ImagesList(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["images"] = base::json::array();
			const CMap &Map = Context.m_Editor.Map();
			for(std::size_t i = 0; i < Map.m_vImages.size(); i++)
			{
				const CImage &Image = Map.m_vImages[i];
				base::json Entry = base::json::object();
				Entry["index"] = static_cast<int>(i);
				Entry["name"] = Image.m_Name;
				Entry["width"] = Image.m_Width;
				Entry["height"] = Image.m_Height;
				Entry["embedded"] = Image.IsEmbedded();
				Entry["bytes"] = static_cast<long long>(Image.m_Data.size());
				Entry["references"] = Context.m_Editor.ImageReferenceCount(static_cast<int>(i));
				Result["images"].push_back(std::move(Entry));
			}
			Result["unused"] = Context.m_Editor.UnusedImage();
			return Context.Ok(std::move(Result));
		}

		bool ImagesAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			// three ways in: a file, the version's mapres directory, or raw
			// pixels, which is what an agent that generated a tileset has
			const std::string Path = Context.Str("path");
			const std::string Mapres = Context.Str("mapres");
			const std::string Data = Context.Str("rgba_base64");
			std::string Error;
			int Index = -1;
			if(!Path.empty())
				Index = Editor.AddImageFromFile(Path, &Error);
			else if(!Mapres.empty())
				Index = Editor.AddImageFromMapres(Mapres, &Error);
			else if(!Data.empty())
			{
				byte_vector Pixels;
				if(!DecodeBase64(Data, Pixels))
					return Context.Fail("images.add: \"rgba_base64\" is not valid base64");
				const int Width = Context.Int("width", 0);
				const int Height = Context.Int("height", 0);
				if(Width <= 0 || Height <= 0)
					return Context.Fail("images.add needs \"width\" and \"height\" with \"rgba_base64\"");
				Index = Editor.AddImageRGBA(Context.Str("name", "image"), Width, Height, std::move(Pixels), &Error);
			}
			else
				return Context.Fail("images.add needs \"path\", \"mapres\" or \"rgba_base64\"");
			if(Index < 0)
				return Context.Fail(Error.empty() ? "the image could not be added" : Error);

			base::json Result = Changed(Editor, "images.add");
			Result["index"] = Index;
			Result["images"] = Context.m_Editor.Map().Statistics().m_Images;
			return Context.Ok(std::move(Result));
		}

		bool ImagesSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("images.set needs an \"index\"");
			const int Index = Context.Int("index", -1);
			if(Index < 0 || Index >= Editor.ImageCount())
				return Context.Fail(base::Format("there is no image {:d}", Index));
			std::string Error;
			if(Context.Has("name") && !Editor.SetImageName(Index, Context.Str("name")))
				return Context.Fail("the image name cannot be empty");
			if(Context.Has("path") && !Editor.ReplaceImageFromFile(Index, Context.Str("path"), &Error))
				return Context.Fail(Error.empty() ? "the image could not be replaced" : Error);
			if(Context.Bool("embed", false) && !Editor.EmbedImage(Index, &Error))
				return Context.Fail(Error.empty() ? "the image could not be embedded" : Error);
			if(Context.Bool("external", false) && !Editor.ExternaliseImage(Index, &Error))
				return Context.Fail(Error.empty() ? "the image could not be made external" : Error);

			base::json Result = Changed(Editor, "images.set");
			Result["index"] = Index;
			Result["status"] = Editor.Status();
			return Context.Ok(std::move(Result));
		}

		bool ImagesRemove(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("images.remove needs an \"index\"");
			std::string Error;
			if(!Editor.RemoveImage(Context.Int("index", -1), &Error))
				return Context.Fail(Error.empty() ? "the image could not be removed" : Error);
			base::json Result = Changed(Editor, "images.remove");
			Result["images"] = Context.m_Editor.Map().Statistics().m_Images;
			return Context.Ok(std::move(Result));
		}

		bool ImagesExport(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("images.export needs an \"index\"");
			const int Index = Context.Int("index", -1);
			if(Index < 0 || Index >= Editor.ImageCount())
				return Context.Fail(base::Format("there is no image {:d}", Index));
			std::string Path = Context.Str("path");
			if(Path.empty())
				Path = (storage::GetExportDirectory() / (storage::SanitiseName(
					Editor.Map().m_vImages[static_cast<std::size_t>(Index)].m_Name) + ".png")).string();
			std::string Error;
			if(!Editor.ExportImage(Index, Path, &Error))
				return Context.Fail(Error.empty() ? "the image could not be written" : Error);
			base::json Result = base::json::object();
			Result["path"] = Path;
			return Context.Ok(std::move(Result));
		}

		bool SoundsList(const CContext &Context)
		{
			base::json Result = base::json::object();
			Result["sounds"] = base::json::array();
			const CMap &Map = Context.m_Editor.Map();
			for(std::size_t i = 0; i < Map.m_vSounds.size(); i++)
			{
				const CSound &Sound = Map.m_vSounds[i];
				base::json Entry = base::json::object();
				Entry["index"] = static_cast<int>(i);
				Entry["name"] = Sound.m_Name;
				Entry["external"] = Sound.m_External != 0;
				Entry["bytes"] = static_cast<long long>(Sound.m_Data.size());
				Entry["references"] = Context.m_Editor.SoundReferenceCount(static_cast<int>(i));
				Entry["length_seconds"] = static_cast<double>(Context.m_Editor.SoundLength(static_cast<int>(i)));
				Result["sounds"].push_back(std::move(Entry));
			}
			Result["unused"] = Context.m_Editor.UnusedSound();
			Result["preview_supported"] = Context.m_Editor.SoundPreviewName().empty() ||
				Context.m_Editor.SoundPreviewPlaying();
			return Context.Ok(std::move(Result));
		}

		bool SoundsAdd(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const std::string Path = Context.Str("path");
			const std::string Resource = Context.Str("resource");
			const std::string Data = Context.Str("opus_base64");
			std::string Error;
			int Index = -1;
			if(!Path.empty())
				Index = Editor.AddSoundFromFile(Path, &Error);
			else if(!Resource.empty())
				Index = Editor.AddSoundFromResource(Resource, &Error);
			else if(!Data.empty())
			{
				byte_vector Bytes;
				if(!DecodeBase64(Data, Bytes))
					return Context.Fail("sounds.add: \"opus_base64\" is not valid base64");
				Index = Editor.AddSoundData(Context.Str("name", "sound"), std::move(Bytes), true, &Error);
			}
			else
				return Context.Fail("sounds.add needs \"path\", \"resource\" or \"opus_base64\"");
			if(Index < 0)
				return Context.Fail(Error.empty() ? "the sound could not be added" : Error);

			base::json Result = Changed(Editor, "sounds.add");
			Result["index"] = Index;
			Result["name"] = Editor.Map().m_vSounds[static_cast<std::size_t>(Index)].m_Name;
			Result["length_seconds"] = static_cast<double>(Editor.SoundLength(Index));
			return Context.Ok(std::move(Result));
		}

		bool SoundsSet(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("sounds.set needs an \"index\"");
			const int Index = Context.Int("index", -1);
			if(Index < 0 || Index >= Editor.SoundCount())
				return Context.Fail(base::Format("there is no sound {:d}", Index));
			std::string Error;
			if(Context.Has("name") && !Editor.SetSoundName(Index, Context.Str("name")))
				return Context.Fail("the sound name cannot be empty");
			if(Context.Bool("embed", false) && !Editor.EmbedSound(Index, &Error))
				return Context.Fail(Error.empty() ? "the sound could not be embedded" : Error);
			if(Context.Bool("external", false) && !Editor.ExternaliseSound(Index, &Error))
				return Context.Fail(Error.empty() ? "the sound could not be made external" : Error);
			return Context.Ok(Changed(Editor, "sounds.set"));
		}

		bool SoundsRemove(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("sounds.remove needs an \"index\"");
			std::string Error;
			if(!Editor.RemoveSound(Context.Int("index", -1), &Error))
				return Context.Fail(Error.empty() ? "the sound could not be removed" : Error);
			return Context.Ok(Changed(Editor, "sounds.remove"));
		}

		bool SoundsExport(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("index"))
				return Context.Fail("sounds.export needs an \"index\"");
			const int Index = Context.Int("index", -1);
			if(Index < 0 || Index >= Editor.SoundCount())
				return Context.Fail(base::Format("there is no sound {:d}", Index));
			std::string Path = Context.Str("path");
			if(Path.empty())
				Path = (storage::GetExportDirectory() / (storage::SanitiseName(
					Editor.Map().m_vSounds[static_cast<std::size_t>(Index)].m_Name) + ".opus")).string();
			std::string Error;
			if(!Editor.ExportSound(Index, Path, &Error))
				return Context.Fail(Error.empty() ? "the sound could not be written" : Error);
			base::json Result = base::json::object();
			Result["path"] = Path;
			return Context.Ok(std::move(Result));
		}

		bool SoundsPreview(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(Context.Bool("stop", false))
			{
				Editor.StopSoundPreview();
				return Context.Ok(Changed(Editor, "sounds.preview"));
			}
			const int Index = Context.Int("index", -1);
			if(!Editor.PreviewSound(Index))
				return Context.Fail(Editor.Status().empty() ? "there is nothing to play" : Editor.Status());
			base::json Result = Changed(Editor, "sounds.preview");
			Result["playing"] = Editor.SoundPreviewName();
			return Context.Ok(std::move(Result));
		}

		bool SoundsAddLayer(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			// a sound layer names its sound, so the index may be given by name
			int Index = Context.Int("index", -1);
			if(Context.Has("name"))
			{
				Index = Editor.FindSound(Context.Str("name"));
				if(Index < 0)
					return Context.Fail(base::Format("there is no sound named '{}'", Context.Str("name").c_str()));
			}
			Editor.AddSoundLayerOp(Index);
			base::json Result = Changed(Editor, "sound_layers.add");
			Result["group"] = Editor.SelectedGroupIndex();
			Result["layer"] = Editor.SelectedLayerIndex();
			return Context.Ok(std::move(Result));
		}

		bool SoundsAddSource(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			const base::json &At = Context.Json("at");
			float X = 0.0f, Y = 0.0f;
			if(At.is_array() && At.size() >= 2)
			{
				X = static_cast<float>(At.at(0).as_double());
				Y = static_cast<float>(At.at(1).as_double());
			}
			const int Source = Editor.AddSoundSource(X, Y);
			if(Source < 0)
				return Context.Fail("sound_sources.add needs a selected sound layer");
			base::json Result = Changed(Editor, "sound_sources.add");
			Result["source"] = Source;
			Result["position"] = base::json::array();
			Result["position"].push_back(static_cast<double>(X));
			Result["position"].push_back(static_cast<double>(Y));
			return Context.Ok(std::move(Result));
		}

		bool SoundsRemoveSource(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Context.Has("source"))
				return Context.Fail("sound_sources.remove needs a \"source\"");
			if(!Editor.RemoveSoundSource(Context.Int("source", -1)))
				return Context.Fail("the source could not be removed");
			return Context.Ok(Changed(Editor, "sound_sources.remove"));
		}

		bool SoundsSetLayerSound(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			int Index = Context.Int("index", -1);
			if(Context.Has("name"))
			{
				Index = Editor.FindSound(Context.Str("name"));
				if(Index < 0)
					return Context.Fail(base::Format("there is no sound named '{}'", Context.Str("name").c_str()));
			}
			if(!Editor.SetSoundLayerSound(Index))
				return Context.Fail("select a sound layer first");
			return Context.Ok(Changed(Editor, "sound_layers.set_sound"));
		}
	} // namespace

	namespace
	{
		bool Undo(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Editor.CanUndo())
				return Context.Fail("there is nothing to undo");
			Editor.UndoOp();
			base::json Result = Changed(Editor, "undo");
			Result["status"] = Editor.Status();
			return Context.Ok(std::move(Result));
		}

		bool Redo(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;
			if(!Editor.CanRedo())
				return Context.Fail("there is nothing to redo");
			Editor.RedoOp();
			base::json Result = Changed(Editor, "redo");
			Result["status"] = Editor.Status();
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ---------------------------------------------------------------- render
	namespace
	{
		/*
			The headless renderer as a method, so a script or an agent can picture
			an area without a window. `path` is where the PNG lands; the rest of
			the options default to the whole map at a viewable size.
		*/
		bool RenderMap(const CContext &Context)
		{
			CEditor &Editor = Context.m_Editor;

			CMapRenderOptions Options;
			const base::json &Size = Context.Json("size");
			if(Size.is_array() && Size.size() >= 2)
			{
				Options.m_Width = std::max(1, static_cast<int>(Size.at(0).as_int(800)));
				Options.m_Height = std::max(1, static_cast<int>(Size.at(1).as_int(600)));
			}
			const base::json &Center = Context.Json("center");
			if(Center.is_array() && Center.size() >= 2)
			{
				Options.m_Center = base::vec2(static_cast<float>(Center.at(0).as_double(0.0)),
					static_cast<float>(Center.at(1).as_double(0.0)));
				Options.m_bCenterSet = true;
			}
			Options.m_Zoom = static_cast<float>(Context.Json("zoom").as_double(1.0));
			Options.m_Time = static_cast<float>(Context.Json("time").as_double(0.0));
			Options.m_bTransparent = Context.Bool("transparent", false);
			const base::json &Background = Context.Json("background");
			if(Background.is_array() && Background.size() >= 3)
				Options.m_Background = CColor{static_cast<int>(Background.at(0).as_int(24)),
					static_cast<int>(Background.at(1).as_int(26)), static_cast<int>(Background.at(2).as_int(30)), 255};
			else if(Background.is_string())
			{
				std::string Text = Background.as_string();
				if(!Text.empty() && Text[0] == '#')
					Text = Text.substr(1);
				const unsigned long Rgb = std::strtoul(Text.c_str(), nullptr, 16);
				Options.m_Background = CColor{static_cast<int>((Rgb >> 16) & 0xFF),
					static_cast<int>((Rgb >> 8) & 0xFF), static_cast<int>(Rgb & 0xFF), 255};
			}

			// the render installs the map's own version's mapres paths; the
			// editor's set is put back so a later draw of the session is unaffected
			const std::string RestoreVersion = Editor.VersionId();
			storage::byte_vector Pixels;
			CMapRenderStats Stats;
			std::string Error;
			if(!RenderMapToRgba(Editor.Map(), Options, Pixels, Stats, Error))
			{
				SetupResourcePaths(RestoreVersion);
				return Context.Fail(Error);
			}
			SetupResourcePaths(RestoreVersion);

			std::string Path = Context.Str("path");
			if(Path.empty())
			{
				const std::string Name = Editor.ProjectName().empty() ? "untitled" : Editor.ProjectName();
				Path = (storage::GetExportDirectory() / (storage::SanitiseName(Name) + ".render.png")).string();
			}
			// Path is a std::string here, but storage::path_text() keeps one
			// spelling for a path and a string and never narrows a wide native
			// path on Windows
			if(!base::WritePng(storage::path(Path), Options.m_Width, Options.m_Height, Pixels))
				return Context.Fail(base::Format("could not write '{}'", storage::path_text(Path)));

			base::json Result = base::json::object();
			Result["path"] = Path;
			Result["width"] = Options.m_Width;
			Result["height"] = Options.m_Height;
			Result["groups"] = Stats.m_Groups;
			Result["layers"] = Stats.m_Layers;
			Result["tiles"] = Stats.m_Tiles;
			Result["quads"] = Stats.m_Quads;
			Result["images"] = Stats.m_Images;
			Result["missing_images"] = Stats.m_MissingImages;
			return Context.Ok(std::move(Result));
		}
	} // namespace

	// ------------------------------------------------------------------- table
	const std::vector<CDescriptor> &Table()
	{
		static const std::vector<CDescriptor> s_aTable = {
			{"agent.version", "", "the name, version and export targets of this build", AgentVersion},
			{"agent.describe", "", "every method, its parameters and what it does", AgentDescribe},
			{"agent.ping", "", "a round trip, so a client can check the session is alive", AgentPing},

			{"project.get", "", "the project: its metadata, its rules, and where its files live", ProjectGet},
			{"project.create", "name, target, width, height, author, allow_extensions",
				"a new project with the standard game and tiles groups", ProjectCreate},
			{"project.open", "path", "open a project file, or a name in the projects directory", ProjectOpen},
			{"project.save", "path, name", "write the project; without a path, under the projects directory", ProjectSave},
			{"project.list", "", "the projects in the application data's projects directory", ProjectList},
			{"project.set_meta", "name, author, description, version, credits, license", "the project and map metadata", ProjectSetMeta},
			{"project.set_target", "target", "the export target: 06, 07, ddnet06, ddnet07 or 08", ProjectSetTarget},

			{"file.open", "path", "open a map, a .unitwp package or a project", FileOpen},
			{"rules.import", "path", "read a rules file: a Uniteed rules JSON or a DDRace .rules text", FileImportRules},

			{"map.get", "tiles", "the whole map; with tiles: true, every tile of every layer", MapGet},
			{"map.set_info", "author, version, credits, license", "the map info strings", MapSetInfo},
			{"map.set_extensions", "allow", "the DDRace extensions switch, and the warnings it produces", MapSetExtensions},
			{"map.validate", "", "check the map for the mistakes a client would refuse", MapValidate},

			{"groups.list", "", "the groups with their parallax, offset and layer count", GroupsList},
			{"groups.add", "name", "a new group", GroupsAdd},
			{"groups.set", "group, name, parallax, offset, clipping, clip", "a group's properties", GroupsSet},
			{"groups.delete", "group", "remove a group and its layers", GroupsDelete},

			{"layers.list", "", "every layer of every group, without its tiles", LayersList},
			{"layers.get", "group, layer, tiles", "one layer; with tiles: true, its tile array", LayersGet},
			{"layers.add", "group, kind, name, width, height, image, sound",
				"a layer: tiles, game, tele, speedup, front, switch, tune, quads or sounds", LayersAdd},
			{"layers.set", "group, layer, name, image, detail, flags, color_env, pos_env", "a layer's properties", LayersSet},
			{"layers.delete", "group, layer", "remove a layer", LayersDelete},
			{"layers.reorder", "group, from, to", "move a layer inside its group", LayersReorder},

			{"tiles.get", "group, layer, x, y", "one tile, or the whole layer when x and y are absent", TilesGet},
			{"tiles.set", "group, layer, x, y, index, flags", "one tile", TilesSet},
			{"tiles.fill", "group, layer, region, index, flags", "a rectangle of one tile; region is x, y, width, height", TilesFill},
			{"tiles.set_region", "group, layer, x, y, width, height, tiles",
				"a rectangle from the tile encoding layers.get returns", TilesSetRegion},
			{"tiles.resize", "width, height, group, layer", "resize the tile layers of the map", TilesResize},

			{"game.list_entities", "", "the entity names a game layer understands", GameListEntities},
			{"game.list_entities_in_map", "", "every entity already placed, with its position", GameListEntitiesInMap},
			{"game.set_entity", "x, y, entity, rotation, group, layer",
				"place an entity on the game layer by name or index", GameSetEntity},

			{"physics.add", "kind", "add an empty DDRace physics layer: tele, speedup, front, switch or tune", PhysicsAdd},
			{"physics.get", "group, layer, x, y", "the payload of a physics layer, one cell or all of it", PhysicsGet},
			{"physics.set", "group, layer, x, y, fields", "write one cell of a physics payload", PhysicsSet},

			{"images.list", "", "every image of the map: name, size, whether it is embedded, and how many layers use it", ImagesList},
			{"images.add", "path, mapres, name, width, height, rgba_base64",
				"add an image from a file, from the version's mapres directory, or from raw RGBA in base64", ImagesAdd},
			{"images.set", "index, name, path, embed, external",
				"rename, replace, embed or externalise an image", ImagesSet},
			{"images.export", "index, path", "write an image out as a PNG", ImagesExport},
			{"images.remove", "index", "remove an image and renumber the layers that used it", ImagesRemove},

			{"sounds.list", "", "every sound of the map: name, whether it is embedded, its length and its users", SoundsList},
			{"sounds.add", "path, resource, name, opus_base64",
				"add a sound from a file, from the version's audio directory, or from base64 opus bytes", SoundsAdd},
			{"sounds.set", "index, name, embed, external", "rename, embed or externalise a sound", SoundsSet},
			{"sounds.export", "index, path", "write a sound out as an .opus file", SoundsExport},
			{"sounds.remove", "index", "remove a sound and renumber the sound layers that used it", SoundsRemove},
			{"sounds.preview", "index, stop", "play a sound through the editor's audio device, or stop it", SoundsPreview},
			{"sound_layers.add", "index, name", "a sound layer that plays one sound resource", SoundsAddLayer},
			{"sound_layers.set_sound", "index, name", "choose which sound the selected sound layer plays", SoundsSetLayerSound},
			{"sound_sources.add", "at", "place a sound source on the selected sound layer; at is [x, y]", SoundsAddSource},
			{"sound_sources.remove", "source", "remove a sound source by index", SoundsRemoveSource},

			{"envelopes.list", "", "every envelope, and which one the selection uses", EnvelopesList},
			{"envelopes.get", "envelope", "one envelope and its points, with their bezier tangents", EnvelopesGet},
			{"envelopes.create", "channels, name", "a new envelope", EnvelopesCreate},
			{"envelopes.set", "envelope, name, channels, synchronized", "an envelope's properties", EnvelopesSet},
			{"envelopes.attach", "envelope, to, group, layer, quad",
				"point a layer or a quad at an envelope; to is colour or position", EnvelopesAttach},
			{"envelopes.add_point", "envelope, time, curve, values", "a new point", EnvelopesAddPoint},
			{"envelopes.set_point", "envelope, point, time, curve, values, in_tangent, out_tangent", "one point", EnvelopesSetPoint},
			{"envelopes.delete_point", "envelope, point", "remove a point", EnvelopesDeletePoint},

			{"quads.list", "group, layer", "every quad of a quad layer, with its points, colours and envelopes", QuadsList},
			{"quads.get", "group, layer, quad", "one quad, with the corners as the canvas draws them", QuadsGet},
			{"quads.add", "group, layer, at, center, size, points, color, colors, pos_env, color_env, tex_size, texcoords, snap",
				"a new quad: a rectangle at, with a size, from a colour or four, or a whole quad from points", QuadsAdd},
			{"quads.move", "group, layer, quad, quads, all, move, at, snap",
				"move quads by a delta, or put a corner at a point", QuadsMove},
			{"quads.set_point", "group, layer, quad, quads, all, point, at, snap",
				"one point of a quad: a corner, or the pivot when point is 4", QuadsSetPoint},
			{"quads.set_points", "group, layer, quad, quads, all, points, snap",
				"the four corners of a quad, and the pivot, in one call", QuadsSetPoints},
			{"quads.set_color", "group, layer, quad, quads, all, color, colors, corner",
				"the corner colours of a quad: one colour, one per corner, or one corner", QuadsSetColor},
			{"quads.set_texture", "group, layer, image, quad, quads, all, texcoords, tex_size",
				"the image a quad layer shows and the texture coordinates of its quads", QuadsSetTexture},
			{"quads.set_envelope", "group, layer, quad, quads, all, pos_env, color_env, pos_env_offset, color_env_offset",
				"point quads at a position or colour envelope, or at none", QuadsSetEnvelope},
			{"quads.snap", "group, layer, quad, quads, all, step",
				"round quads to a grid, 32 pixels by default", QuadsSnap},
			{"quads.duplicate", "group, layer, quad, quads, all, move, to_group, to_layer",
				"copy quads, in their own layer or another one", QuadsDuplicate},
			{"quads.delete", "group, layer, quad, quads, all", "remove quads from a quad layer", QuadsDelete},
			{"quads.pick", "group, layer, at, time, select",
				"the quad under a point of the map, the way the canvas picks one", QuadsPick},

			{"rules.get", "", "the project's autorules document", RulesGet},
			{"rules.set", "(an autorules document)", "replace the autorules document", RulesSet},
			{"rules.new", "name", "a fresh rule set with one empty configuration", RulesNew},
			{"rules.add_config", "name", "a new configuration", RulesAddConfig},
			{"rules.add_run", "config", "a new run in a configuration", RulesAddRun},
			{"rules.add_rule", "config, run, tile, flags, name, when, modulo, random, skip_empty, skip_full",
				"a new index rule, with its conditions, in one call", RulesAddRule},
			{"rules.delete", "level, config, run, rule",
				"delete a configuration, a run or an index rule; level is config, run or rule", RulesDelete},
			{"rules.apply", "config, target, group, layer, all, seed",
				"run the rules over a tile layer; target is an engine slot", RulesApply},
			{"rules.export", "path, format", "write the rules to a file", RulesExport},

			{"live.get", "group, layer", "the live tiles of a layer: its source layer and its source-to-output pairs", LiveGet},
			{"live.set", "group, layer, enabled, source_group, source_layer, maps",
				"set a layer's live tiles, a list of source, target, flags pairs, and run the pass", LiveSet},

			{"export.map", "target, path, project", "write the map for a target; without a path, under the export directory", ExportMap},
			{"export.all", "targets, project", "write the map for every target, or for the named ones", ExportAll},
			{"export.package", "path, project", "write a .unitwp package with the mapres files it needs; \"project\" opens that project first", ExportPackage},

			{"render.map", "path, center, size, zoom, time, transparent, background",
				"draw the map's tiles and quads to a PNG with no window; center is [x,y] world pixels, size is [w,h] pixels", RenderMap},

			{"select.get", "", "the selection, the brush, the target and the extensions gate", SelectGet},
			{"select.set", "group, layer", "select a layer", SelectSet},
			{"select.brush", "tile, size, flags", "the tile brush", SelectBrush},
			{"select.quad", "quad, quads", "the quad selection of a quad layer", SelectQuad},
			{"select.view", "zoom, center, grid, fit", "the camera", SelectView},

			{"undo", "", "undo the last edit", Undo},
			{"redo", "", "redo the last undo", Redo},
		};
		return s_aTable;
	}

	const CDescriptor *Find(const std::string &Method)
	{
		for(const CDescriptor &Descriptor : Table())
			if(Method == Descriptor.m_pName)
				return &Descriptor;
		return nullptr;
	}

	base::json Describe()
	{
		base::json List = base::json::array();
		for(const CDescriptor &Descriptor : Table())
		{
			base::json Entry = base::json::object();
			Entry["method"] = Descriptor.m_pName;
			Entry["params"] = Descriptor.m_pParams;
			Entry["description"] = Descriptor.m_pDescription;
			List.push_back(std::move(Entry));
		}
		return List;
	}
} // namespace game::admin::ops
