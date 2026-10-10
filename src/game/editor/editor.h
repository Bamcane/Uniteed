#ifndef GAME_EDITOR_EDITOR_H
#define GAME_EDITOR_EDITOR_H

/*
	The editor: a map model, a camera, a brush and the panels around it.
	Everything is immediate mode, nanovg draws the map and the interface in the
	same pass, so there is no retained widget tree to keep in sync.

	Keys
		middle / right drag   pan          wheel         zoom
		left                  paint        right (quick) pick the tile under the cursor
		W A S D / arrows      pan          F             fit the map
		G                     grid         1 2 3         paint / erase / pick
		4 5 6 7               brush size 1 2 4 8
		H J K                 toggle hflip / vflip / rotate of the brush
		Q E                   previous / next tile index
		[ ]                   brush size down / up
		P                     paint the whole layer with the brush
		Ctrl+Z / Ctrl+Y       undo / redo
		Ctrl+N                new map
		Ctrl+O                open
		Ctrl+S                save the .unitwp package
		Ctrl+E                export, choosing the target version
		Ctrl+L                add a tile layer to the selected group
		Ctrl+G                add a group
		Delete                delete the selected layer (Shift for a group)
		Ctrl+Q                quit
*/

#include <string>
#include <vector>

#include "base/math.h"
#include "engine/graphics.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"

namespace game
{
	enum class editor_tool
	{
		paint,
		erase,
		pick,
	};

	struct CLayerView
	{
		bool m_Visible = true;
		bool m_Locked = false;
	};

	class CEditor
	{
	public:
		bool Init(engine::CGraphics *pGraphics);
		void Update();
		void Render();

		// file operations, also used by the command line and the screenshot mode
		bool OpenFile(const std::string &Path);
		bool SavePackage(const std::string &Path);
		bool ExportMap(const std::string &Path, map_target Target);
		void NewMap(int Width, int Height);

		void SetStatus(const std::string &Message);
		const std::string &Status() const { return m_Status; }
		bool Dirty() const { return m_Dirty; }
		const std::string &SourcePath() const { return m_SourcePath; }
		const CMap &Map() const { return m_Map; }
		map_target Target() const { return m_Target; }

		// camera helpers, used by the offscreen screenshot mode
		void FitToMap();
		void SetZoom(float Zoom) { m_View.m_Zoom = Zoom; }
		bool Grid() const { return m_bGrid; }
		void SetGrid(bool Grid) { m_bGrid = Grid; }
		int BrushSize() const { return m_BrushSize; }
		void SetBrushSize(int Size) { m_BrushSize = base::clamp(Size, 1, 16); }
		const std::string &VersionId() const { return m_VersionId; }
		void SetCenter(base::vec2 Center) { m_View.m_Center = Center; }
		void SetTarget(map_target Target);
		void SetSelectedLayer(int Group, int Layer);
		void SetBrushTile(int Index) { m_BrushTile = Index; }
		void SetCursor(base::vec2 World) { m_CursorWorld = World; m_bCursorValid = true; }

	private:
		// ------------------------------------------------------------- helpers
		CLayer *SelectedLayer();
		const CLayer *SelectedLayer() const;
		CGroup *SelectedGroup();
		CLayerView *ViewState(int Group, int Layer);
		bool EnsureViewStates();
		void ClampSelection();

		int TextureForImage(int ImageIndex);
		bool SetupVersionForMap();

		// -------------------------------------------------------------- actions
		void HandleShortcuts();
		void HandleCanvasInput(const base::rect &Canvas);
		void HandlePopupInput();
		void OpenBrowser();
		void PaintAt(base::vec2 World);
		void BeginStroke();
		void EndStroke();
		void PushUndo(int Group, int Layer);
		void Undo();
		void Redo();
		void ResetTextures();

		// ------------------------------------------------------------- drawing
		void DrawMap(NVGcontext *pContext, const base::rect &Canvas);
		void DrawTileLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer, bool Selected);
		void DrawQuadLayer(NVGcontext *pContext, const CLayer &Layer);
		void DrawGameLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer);
		void DrawPhysicsLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer, int Flags);
		void DrawCursor(NVGcontext *pContext, const base::rect &Canvas);
		void DrawToolbar(NVGcontext *pContext, const base::rect &Area);
		void DrawLayerPanel(NVGcontext *pContext, const base::rect &Area);
		void DrawPalettePanel(NVGcontext *pContext, const base::rect &Area);
		void DrawStatusBar(NVGcontext *pContext, const base::rect &Area);
		void DrawPopups(NVGcontext *pContext);
		void DrawOpenPopup(NVGcontext *pContext);
		void DrawSavePopup(NVGcontext *pContext);
		void DrawExportPopup(NVGcontext *pContext);
		void DrawHelpPopup(NVGcontext *pContext);

		// ---------------------------------------------------------------- state
		engine::CGraphics *m_pGraphics = nullptr;

		CMap m_Map;
		CUnitwpPackage m_Package;
		bool m_bPackageLoaded = false;
		std::string m_SourcePath;
		std::string m_VersionId = "07";
		map_target m_Target = map_target::tee08;
		bool m_Dirty = false;

		base::view_transform m_View;
		bool m_bGrid = true;
		bool m_bShowDetail = true;

		int m_SelectedGroup = 0;
		int m_SelectedLayer = 0;
		std::vector<std::vector<CLayerView>> m_vViewStates;

		editor_tool m_Tool = editor_tool::paint;
		int m_BrushTile = 1;
		int m_BrushFlags = 0;
		int m_BrushSize = 1;
		bool m_bPainting = false;
		base::ivec2 m_StrokeLastTile{-1, -1};

		base::vec2 m_CursorWorld;
		bool m_bCursorValid = false;

		// the palette of the active image, if any
		float m_PaletteScroll = 0.0f;
		int m_PaletteHover = -1;
		float m_LayerScroll = 0.0f;

		// popups
		bool m_bShowOpen = false;
		bool m_bShowSave = false;
		bool m_bShowExport = false;
		bool m_bShowHelp = false;
		std::string m_TextInput;
		std::string m_SavePath;
		std::vector<std::string> m_vBrowserEntries;
		int m_BrowserSelected = 0;
		float m_BrowserScroll = 0.0f;
		int m_ExportTarget = 3; // index into AllTargets plus the package entry

		// undo
		struct CUndoState
		{
			int m_Group = 0;
			int m_Layer = 0;
			int m_Width = 0;
			int m_Height = 0;
			std::vector<CTile> m_vTiles;
		};
		std::vector<CUndoState> m_vUndo;
		std::vector<CUndoState> m_vRedo;

		std::string m_Status;
		double m_StatusTime = 0.0;
	};
} // namespace game

#endif // GAME_EDITOR_EDITOR_H
