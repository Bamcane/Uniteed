/*
	The administrative interface: the JSON methods an agent or a script calls.

	Every method is a member of one table, so `agent.describe` and the dispatch
	cannot drift apart: a method that is listed is a method that runs. The
	bodies live in admin.cpp and admin_edit.cpp, split by whether they touch the
	map geometry or only its metadata.

	The rule this file follows: a method returns what it changed, in the same
	shape `map.get` uses, so a caller can apply a batch of edits and then read
	the result back without a second call.
*/

#include "admin.h"

#include <algorithm>
#include <cstring>

#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "game/editor/admin_ops.h"
#include "game/editor/editor.h"
#include "game/editor/editor.h"
#include "game/map/envelope.h"

namespace game::admin
{
	namespace
	{
		// the layer kind as the JSON interface spells it
		const char *LayerKindName(const CLayer &Layer)
		{
			if(Layer.IsGameLayer())
				return "game";
			if(Layer.IsPhysicsLayer())
			{
				if(Layer.m_Flags & TILESLAYERFLAG_TELE) return "tele";
				if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP) return "speedup";
				if(Layer.m_Flags & TILESLAYERFLAG_FRONT) return "front";
				if(Layer.m_Flags & TILESLAYERFLAG_SWITCH) return "switch";
				if(Layer.m_Flags & TILESLAYERFLAG_TUNE) return "tune";
			}
			if(Layer.IsTiles())
				return "tiles";
			if(Layer.IsQuads())
				return "quads";
			if(Layer.IsSounds())
				return "sounds";
			return "unknown";
		}

	} // namespace

	// --------------------------------------------------------------- snapshots
	base::json LayerToJson(const CEditor &Editor, int Group, int Layer, bool bIncludeTiles)
	{
		const CLayer *pLayer = Editor.Layer(Group, Layer);
		base::json Json = base::json::object();
		if(pLayer == nullptr)
			return Json;
		const CLayer &Ref = *pLayer;

		Json["group"] = Group;
		Json["index"] = Layer;
		Json["name"] = Ref.m_Name;
		Json["kind"] = LayerKindName(Ref);
		Json["type"] = Ref.m_Type;
		Json["flags"] = Ref.m_Flags;
		Json["detail"] = Ref.IsDetailLayer();
		Json["physics"] = Ref.IsPhysicsLayer();
		Json["game"] = Ref.IsGameLayer();
		Json["slot"] = Editor.SlotOfLayer(Group, Layer);

		if(Ref.IsTiles())
		{
			Json["width"] = Ref.m_Width;
			Json["height"] = Ref.m_Height;
			Json["image"] = Ref.m_Image;
			Json["color"] = base::json::array();
			Json["color"].push_back(Ref.m_Color.r);
			Json["color"].push_back(Ref.m_Color.g);
			Json["color"].push_back(Ref.m_Color.b);
			Json["color"].push_back(Ref.m_Color.a);
			Json["color_env"] = Ref.m_ColorEnv;
			Json["tilemap_version"] = Ref.m_SourceTilemapVersion;

			int NonEmpty = 0;
			for(const CTile &Tile : Ref.m_vTiles)
				if(Tile.m_Index != 0)
					NonEmpty++;
			Json["non_empty"] = NonEmpty;

			if(bIncludeTiles)
			{
				// A tile is written as its index, and only the flags that are
				// set follow it, so a mostly empty layer stays readable. An
				// index of 0 with no flags is a bare 0 in the array.
				base::json Tiles = base::json::array();
				for(const CTile &Tile : Ref.m_vTiles)
				{
					if(Tile.m_Flags == 0)
						Tiles.push_back(static_cast<int>(Tile.m_Index));
					else
					{
						base::json Entry = base::json::object();
						Entry["i"] = static_cast<int>(Tile.m_Index);
						Entry["f"] = static_cast<int>(Tile.m_Flags);
						Tiles.push_back(std::move(Entry));
					}
				}
				Json["tiles"] = std::move(Tiles);
			}
		}
		else if(Ref.IsQuads())
		{
			Json["image"] = Ref.m_Image;
			base::json Quads = base::json::array();
			for(const CQuad &Quad : Ref.m_vQuads)
			{
				base::json Entry = base::json::object();
				base::json Points = base::json::array();
				for(int i = 0; i < 5; i++)
				{
					base::json Point = base::json::array();
					Point.push_back(Quad.m_aPoints[i].x);
					Point.push_back(Quad.m_aPoints[i].y);
					Points.push_back(std::move(Point));
				}
				Entry["points"] = std::move(Points);
				base::json Colors = base::json::array();
				for(int i = 0; i < 4; i++)
				{
					base::json Color = base::json::array();
					Color.push_back(Quad.m_aColors[i].r);
					Color.push_back(Quad.m_aColors[i].g);
					Color.push_back(Quad.m_aColors[i].b);
					Color.push_back(Quad.m_aColors[i].a);
					Colors.push_back(std::move(Color));
				}
				Entry["colors"] = std::move(Colors);
				Entry["pos_env"] = Quad.m_PosEnv;
				Entry["color_env"] = Quad.m_ColorEnv;
				Quads.push_back(std::move(Entry));
			}
			Json["quads"] = std::move(Quads);
		}
		else if(Ref.IsSounds())
		{
			Json["sound"] = Ref.m_Sound;
			base::json Sources = base::json::array();
			for(const CSoundSource &Source : Ref.m_vSources)
			{
				base::json Entry = base::json::object();
				Entry["x"] = Source.m_Position.x;
				Entry["y"] = Source.m_Position.y;
				Entry["loop"] = Source.m_Loop;
				Entry["pan"] = Source.m_Pan;
				Entry["time_delay"] = Source.m_TimeDelay;
				Entry["falloff"] = Source.m_Falloff;
				Entry["pos_env"] = Source.m_PosEnv;
				Entry["sound_env"] = Source.m_SoundEnv;
				Entry["shape"] = Source.m_Shape.m_Type;
				Entry["shape_width"] = Source.m_Shape.m_Width;
				Entry["shape_height"] = Source.m_Shape.m_Height;
				Sources.push_back(std::move(Entry));
			}
			Json["sources"] = std::move(Sources);
		}
		return Json;
	}

	base::json MapToJson(const CEditor &Editor, bool bIncludeTiles)
	{
		const CMap &Map = Editor.Map();
		const CMapStatistics Stats = Map.Statistics();

		base::json Json = base::json::object();
		Json["datafile_version"] = Map.m_SourceDatafileVersion;
		Json["target"] = GetTargetProfile(Editor.Target()).m_pId;
		Json["assets"] = Editor.VersionId();
		Json["author"] = Map.m_aAuthor;
		Json["version"] = Map.m_aVersion;
		Json["credits"] = Map.m_aCredits;
		Json["license"] = Map.m_aLicense;
		Json["has_ddnet_items"] = Map.m_SourceHasDdnetItems;
		Json["allow_extensions"] = Map.m_bAllowExtensions;
		Json["dirty"] = Editor.Dirty();

		base::json Counts = base::json::object();
		Counts["groups"] = Stats.m_Groups;
		Counts["layers"] = Stats.m_Layers;
		Counts["tiles"] = Stats.m_Tiles;
		Counts["non_empty_tiles"] = Stats.m_TilesWithContent;
		Counts["images"] = Stats.m_Images;
		Counts["embedded_images"] = Stats.m_EmbeddedImages;
		Counts["envelopes"] = Stats.m_Envelopes;
		Counts["quads"] = Stats.m_Quads;
		Counts["preserved_items"] = Stats.m_Extras;
		Json["counts"] = std::move(Counts);

		base::json Groups = base::json::array();
		for(std::size_t g = 0; g < Map.m_vGroups.size(); g++)
		{
			const CGroup &Group = Map.m_vGroups[g];
			base::json GroupJson = base::json::object();
			GroupJson["index"] = static_cast<int>(g);
			GroupJson["name"] = Group.m_Name;
			GroupJson["offset"] = base::json::array();
			GroupJson["offset"].push_back(Group.m_OffsetX);
			GroupJson["offset"].push_back(Group.m_OffsetY);
			GroupJson["parallax"] = base::json::array();
			GroupJson["parallax"].push_back(Group.m_ParallaxX);
			GroupJson["parallax"].push_back(Group.m_ParallaxY);
			GroupJson["clipping"] = Group.m_UseClipping != 0;
			if(Group.m_UseClipping)
			{
				GroupJson["clip"] = base::json::array();
				GroupJson["clip"].push_back(Group.m_ClipX);
				GroupJson["clip"].push_back(Group.m_ClipY);
				GroupJson["clip"].push_back(Group.m_ClipW);
				GroupJson["clip"].push_back(Group.m_ClipH);
			}
			base::json Layers = base::json::array();
			for(std::size_t l = 0; l < Group.m_vLayers.size(); l++)
				Layers.push_back(LayerToJson(Editor, static_cast<int>(g), static_cast<int>(l), bIncludeTiles));
			GroupJson["layers"] = std::move(Layers);
			Groups.push_back(std::move(GroupJson));
		}
		Json["groups"] = std::move(Groups);

		base::json Images = base::json::array();
		for(const CImage &Image : Map.m_vImages)
		{
			base::json Entry = base::json::object();
			Entry["name"] = Image.m_Name;
			Entry["width"] = Image.m_Width;
			Entry["height"] = Image.m_Height;
			Entry["embedded"] = Image.IsEmbedded();
			Entry["bytes"] = static_cast<long long>(Image.m_Data.size());
			Images.push_back(std::move(Entry));
		}
		Json["images"] = std::move(Images);

		base::json Envelopes = base::json::array();
		for(const CEnvelope &Envelope : Map.m_vEnvelopes)
		{
			base::json Entry = base::json::object();
			Entry["name"] = Envelope.m_Name;
			Entry["channels"] = Envelope.m_Channels;
			Entry["synchronized"] = Envelope.m_Synchronized;
			Entry["bezier"] = Envelope.UsesBezier();
			Entry["length_ms"] = Envelope.m_vPoints.empty() ? 0 : Envelope.m_vPoints.back().m_Time;
			base::json Points = base::json::array();
			for(const CEnvelopePoint &Point : Envelope.m_vPoints)
			{
				base::json PointJson = base::json::object();
				PointJson["time"] = Point.m_Time;
				PointJson["curve"] = Point.m_Curvetype;
				base::json Values = base::json::array();
				for(int c = 0; c < 4; c++)
					Values.push_back(Point.m_aValues[c]);
				PointJson["values"] = std::move(Values);
				Points.push_back(std::move(PointJson));
			}
			Entry["points"] = std::move(Points);
			Envelopes.push_back(std::move(Entry));
		}
		Json["envelopes"] = std::move(Envelopes);
		return Json;
	}

	base::json ProjectToJson(const CEditor &Editor)
	{
		const CProject &Project = Editor.Project();
		base::json Json = base::json::object();
		Json["id"] = Project.m_Meta.m_Id;
		Json["name"] = Project.m_Meta.m_Name;
		Json["author"] = Project.m_Meta.m_Author;
		Json["description"] = Project.m_Meta.m_Description;
		Json["created"] = Project.m_Meta.m_Created;
		Json["updated"] = Project.m_Meta.m_Updated;
		Json["target"] = Project.m_Meta.m_Target;
		Json["assets"] = Project.m_Meta.m_Assets;
		Json["path"] = Editor.SourcePath();
		Json["dirty"] = Editor.Dirty();
		Json["projects_directory"] = storage::GetProjectsDirectory().string();
		Json["export_directory"] = storage::GetExportDirectory().string();
		Json["allow_extensions"] = Editor.Map().m_bAllowExtensions;
		Json["rules"] = rules::ToJson(Editor.RuleSet());
		return Json;
	}

	base::json SelectionToJson(const CEditor &Editor)
	{
		base::json Json = base::json::object();
		Json["group"] = Editor.SelectedGroupIndex();
		Json["layer"] = Editor.SelectedLayerIndex();
		const CLayer *pLayer = Editor.Layer(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());
		if(pLayer != nullptr)
		{
			Json["kind"] = LayerKindName(*pLayer);
			Json["name"] = pLayer->m_Name;
			Json["slot"] = Editor.SlotOfLayer(Editor.SelectedGroupIndex(), Editor.SelectedLayerIndex());
		}
		Json["target"] = GetTargetProfile(Editor.Target()).m_pId;
		Json["allow_extensions"] = Editor.Map().m_bAllowExtensions;
		Json["extensions_allowed"] = Editor.GameEntities();
		Json["mode"] = Editor.Mode() == edit_mode::tiles ? "tiles" :
			(Editor.Mode() == edit_mode::quads ? "quads" : "none");
		Json["brush_tile"] = Editor.BrushTileIndex();
		Json["brush_flags"] = Editor.BrushFlagsValue();
		Json["brush_size"] = Editor.BrushSize();
		Json["grid"] = Editor.Grid();
		Json["zoom"] = static_cast<double>(Editor.Zoom());
		Json["envelope_time"] = static_cast<double>(Editor.EnvelopeTime());
		Json["selected_quads"] = base::json::array();
		for(const int Quad : Editor.SelectedQuads())
			Json["selected_quads"].push_back(Quad);
		return Json;
	}

	// ----------------------------------------------------------------- methods
	CSession::CSession()
	{
		m_pEditor = new CEditor();
		m_pEditor->InitHeadless();
		m_bOwnsEditor = true;
	}

	CSession::~CSession()
	{
		if(m_bOwnsEditor)
			delete m_pEditor;
	}

	void CSession::Adopt(CEditor *pEditor)
	{
		if(m_bOwnsEditor && m_pEditor != nullptr)
			delete m_pEditor;
		m_pEditor = pEditor;
		m_bOwnsEditor = false;
	}

	bool CSession::Has(const std::string &Method) const
	{
		return ops::Find(Method) != nullptr;
	}

	CResponse CSession::Run(const std::string &Method, const base::json &Params)
	{
		CResponse Response;
		const ops::CDescriptor *pDescriptor = ops::Find(Method);
		if(pDescriptor == nullptr)
		{
			Response.m_Error = "unknown method '" + Method + "'; call agent.describe for the list";
			return Response;
		}
		if(m_pEditor == nullptr)
		{
			Response.m_Error = "the session has no editor";
			return Response;
		}
		const ops::CContext Context{*m_pEditor, Params, Response};
		if(!pDescriptor->m_pHandler(Context))
			return Response;
		Response.m_bOk = true;
		if(!Response.m_Result.is_object())
			Response.m_Result = base::json::object();
		return Response;
	}

	base::json CSession::Handle(const base::json &Request)
	{
		base::json Answer = base::json::object();
		if(Request.has("id"))
			Answer["id"] = Request["id"];

		const std::string Method = Request["method"].as_string();
		if(Method.empty())
		{
			Answer["ok"] = false;
			Answer["error"] = "a request needs a \"method\"";
			return Answer;
		}
		Answer["method"] = Method;

		const base::json &Params = Request["params"];
		const CResponse Response = Run(Method, Params.is_object() ? Params : base::json::object());
		Answer["ok"] = Response.m_bOk;
		if(Response.m_bOk)
			Answer["result"] = Response.m_Result;
		else
			Answer["error"] = Response.m_Error.empty() ? "the method failed" : Response.m_Error;
		return Answer;
	}

	base::json CSession::Describe()
	{
		return ops::Describe();
	}
} // namespace game::admin
