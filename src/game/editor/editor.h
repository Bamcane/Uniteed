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

#include <map>

#include "base/math.h"
#include "engine/graphics.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"

namespace game
{
	// the fixed metrics of the editor chrome: the toolbar on top, the layer
	// panel on the left, the palette on the right, the status bar at the bottom
	constexpr float ToolbarHeight = 40.0f;
	constexpr float PanelWidth = 268.0f;
	constexpr float StatusHeight = 26.0f;

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

		// the envelope preview clock, so the screenshot mode can pin it down
		void SetEnvelopePlaying(bool Playing) { m_bEnvelopePlaying = Playing; }
		void SetEnvelopeTime(float Time) { m_EnvelopeTime = Time; }
		bool EnvelopePlaying() const { return m_bEnvelopePlaying; }
		float EnvelopeTime() const { return m_EnvelopeTime; }
		// opens the colour picker on the selected tile layer or quads; false when
		// there is nothing to colour
		bool OpenColorPicker();

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
		// undo: the scope says what was touched, so a colour change does not
		// have to snapshot a whole tile layer
		enum class undo_scope
		{
			tiles,
			quads,
			colour,
		};

		// ------------------------------------------------------------- helpers
		CLayer *SelectedLayer();		const CLayer *SelectedLayer() const;
		CGroup *SelectedGroup();
		CLayerView *ViewState(int Group, int Layer);
		bool EnsureViewStates();
		void ClampSelection();

		// the canvas inside the window: the layout, the drawing and the hit
		// testing all go through this one function so they cannot drift apart
		base::rect CanvasRect(const base::vec2 &Window) const;
		// the world rectangle the tile layers cover, which is the map boundary
		base::rect MapBounds() const;

		int TextureForImage(int ImageIndex);
		// the same image with every pixel multiplied by Tint, for the layer
		// colours: nanovg image patterns cannot modulate a texture's rgb
		int TextureForImage(int ImageIndex, const CColor &Tint);
		bool TintSource(int ImageIndex, std::vector<std::byte> &Pixels, int &Width, int &Height);
		std::string TintSlotKey(std::size_t Slot) const;
		bool SetupVersionForMap();

		// ------------------------------------------------------------- colours
		// the effective colour of a tile layer: its own colour times its colour
		// envelope, the way the reference multiplies them
		CColor LayerColor(const CLayer &Layer) const;
		// the four corner colours of a quad with the quad colour envelope
		void QuadColors(const CQuad &Quad, CColor *paColors) const;
		// the quad's points moved and turned by its position envelope
		void QuadPoints(const CQuad &Quad, base::vec2 *paPoints) const;
		// the colour the picker is editing, and whether a quad or a layer owns it
		bool PickerColor(CColor &Color) const;
		void SetPickerColor(const CColor &Color);
		bool PickerTargetsQuad() const;
		std::string PickerTargetName() const;

		// -------------------------------------------------------------- actions
		void HandleShortcuts();
		void HandleCanvasInput(const base::rect &Canvas);
		bool IsQuadLayerSelected() const;
		void UpdateQuadHover(bool bInCanvas);
		void HandlePopupInput();
		void OpenBrowser();
		void PaintAt(base::vec2 World);
		void BeginStroke();
		void EndStroke();
		void PushUndo(int Group, int Layer, undo_scope Scope = undo_scope::tiles);
		void Undo();
		void Redo();
		void ResetTextures();

		// ------------------------------------------------------------- drawing
		void DrawMap(NVGcontext *pContext, const base::rect &Canvas);
		void DrawTileLayer(NVGcontext *pContext, const CLayer &Layer, bool Selected);
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
		void DrawColorPopup(NVGcontext *pContext);
		void DrawEnvelopeBar(NVGcontext *pContext, const base::rect &Area);

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

		// envelope preview: the reference plays the envelopes on a clock the
		// user can pause and speed up, which is what makes colour envelopes and
		// animated quads visible while editing
		float m_EnvelopeTime = 0.0f;
		bool m_bEnvelopePlaying = true;
		float m_EnvelopeSpeed = 1.0f;
		double m_LastFrameTime = 0.0;
		float EnvelopeLoopLength() const;

		// quads selected in the selected quad layer, by index
		std::vector<int> m_vSelectedQuads;
		int m_QuadHover = -1;

		// the colour picker popup
		bool m_bShowColor = false;
		CColor m_PickerColor;
		// the hue survives a trip through grey, which the rgb colour cannot store
		float m_PickerHue = 0.0f;
		bool m_bPickerDragging = false;

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

		struct CUndoState
		{
			undo_scope m_Scope = undo_scope::tiles;
			int m_Group = 0;
			int m_Layer = 0;
			int m_Width = 0;
			int m_Height = 0;
			std::vector<CTile> m_vTiles;
			std::vector<CQuad> m_vQuads;
			CColor m_Color;
		};
		std::vector<CUndoState> m_vUndo;
		std::vector<CUndoState> m_vRedo;
		CUndoState CaptureLayer(const CLayer &Layer, int Group, int LayerIndex, undo_scope Scope) const;
		void ApplyUndoState(const CUndoState &State, std::vector<CUndoState> &Other);

		// the tinted copies of the tile sheets: a small fixed set of slots, so an
		// animated colour envelope cannot grow the texture cache without bound
		static constexpr std::size_t MaxTintSlotsPerImage = 4;
		struct CTintSlot
		{
			int m_Image = -1;
			CColor m_Tint;
			int m_Width = 0;
			int m_Height = 0;
			unsigned m_LastUse = 0;
		};
		std::vector<CTintSlot> m_vTintSlots;
		unsigned m_TintClock = 0;
		// decoded mapres pixels, so a tint rewrite does not decode the png again
		struct CSourceImage
		{
			int m_Width = 0;
			int m_Height = 0;
			std::vector<std::byte> m_Pixels;
		};
		std::map<int, CSourceImage> m_SourceImages;

		std::string m_Status;
		double m_StatusTime = 0.0;
	};
} // namespace game

#endif // GAME_EDITOR_EDITOR_H
