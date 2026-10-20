#ifndef GAME_EDITOR_EDITOR_H
#define GAME_EDITOR_EDITOR_H

/*
	The editor: a map model, a camera, a brush and the panels around it.
	Everything is immediate mode, the painter draws the map and the interface
	in the same pass, so there is no retained widget tree to keep in sync.

	The canvas follows the selected layer: a tile layer is painted with the
	tile brush, a quad layer is edited by its five points. There is no separate
	mode to switch, the layer tree in the lower docker is the switch.

	Every letter names its tool, and E is a toggle rather than a fifth tool,
	the way a paint program binds the eraser.

	Keys
		Tools (a tile layer)
			B                     brush
			E                     eraser, and E again for the tool before it
			F                     fill: flood one tile region
			P                     picker: drag a region of the map into the brush
			V                     move the view
			1 2 3                 brush / eraser / picker
		Tile layers
			left                  paint
			right (quick)         take a brush the size of the current one off the map
			Space                 the same, as a key
			, .                   previous / next tile index
			H J K                 toggle hflip / vflip / rotate of the brush
			Shift+P               paint the whole layer with the brush, tiled
			palette drag          select a region of the sheet as the brush
			pick tool drag        select a region of the map as the brush
		Quad layers
			left                  select a quad, and drag it to move it
			left (shift)          drag the point under the cursor instead
			left (ctrl)           add a quad to the selection
			C                     colour of the selected quad (or of its corner)
		Both
			middle / right drag   pan          wheel         zoom
			W A S D / arrows      pan
			Ctrl+0                fit the map in the window
			G                     grid
			4 5 6 7               brush size 1 2 4 8
			[ ]                   brush size down / up
			Ctrl+Z / Ctrl+Y       undo / redo
			Ctrl+N                new map
			Ctrl+O                open
			Ctrl+S                save the .unitwp package
			Ctrl+E                export, choosing the target version
			Ctrl+L                add a tile layer to the selected group
			Ctrl+Shift+L          add a quad layer to the selected group
			Ctrl+G                add a group
			Delete                delete the selected layer (Shift for a group)
			Ctrl+Q                quit
*/

#include <array>
#include <string>
#include <vector>

#include <map>

#include "base/math.h"
#include "engine/input.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"
#include "game/project.h"
#include "game/rules/rules.h"

#if UNITEED_GUI
#include "engine/audio.h"
#include "engine/file_dialog.h"
#include "engine/vector_textures.h"
#endif

namespace engine
{
	class CGraphics;

	namespace painter
	{
		class CPainter;
	}
}

namespace game
{
	// the fixed metrics of the editor chrome: the menu bar and the toolbar on
	// top, the two docks on the sides, the status bar at the bottom. The dock
	// *widths* are not fixed: they live in the editor's dock state and can be
	// dragged.
	/*
		The two rows of chrome above the canvas, which is what a drawing
		application puts there: a slim menu bar that is also the title bar of
		the window (Files, Settings, the window buttons), and the toolbar under
		it with everything that is about the map rather than about the window.
	*/
	constexpr float MenuBarHeight = 28.0f;
	constexpr float ToolbarHeight = 40.0f;
	// what both rows take from the top of the window, which is where the
	// canvas, the tool box and the dockers start
	constexpr float ChromeHeight = MenuBarHeight + ToolbarHeight;
	constexpr float StatusHeight = 26.0f;
	// the tab strip on top of a dock, which switches between its workspaces
	constexpr float DockTabHeight = 26.0f;
	// the vertical tool strip on the far left, which is what makes the
	// workspace read as a Krita one rather than a row of buttons
	constexpr float ToolBoxWidth = 38.0f;
	/*
		The dockers are stacked on the right, the way Krita's default workspace
		puts them: one area at the top for the tiles, the assets and the curve
		and rule workspaces, and one at the bottom for the layer tree, which is
		what a map maker watches while painting. There is no left dock - the
		tool box owns that edge.
	*/
	// the height of the splitter between the two stacked dockers
	constexpr float DockSplitterHeight = 5.0f;
	constexpr float MinDockHeight = 120.0f;
	// the grab area of a dock edge
	constexpr float DockSplitterWidth = 5.0f;
	// the width a dock starts with, and the smallest one still worth showing
	constexpr float DefaultDockWidth = 340.0f;
	constexpr float MinDockWidth = 160.0f;

	// which of the two stacked dockers an operation is about
	enum class dock_slot
	{
		right_upper,
		right_lower,
	};

	/*
		An entity of a game layer and the name it goes by. One table serves the
		agent interface, which resolves these names, and the interface, which
		prints them next to the sprite they belong to.
	*/
	struct CEntityName
	{
		int m_Index = 0;
		const char *m_pName = nullptr;
	};

	// every entity of the game layer, vanilla and DDRace
	const std::vector<CEntityName> &EntityNames();
	// the entity a name belongs to, -1 when the name is unknown
	int EntityFromName(const std::string &Name);
	// the name of an entity index, nullptr when it has none
	const char *EntityName(int Entity);

	enum class editor_tool
	{
		paint,   // paint what the brush carries
		fill,    // the same brush, dragged over a region
		erase,   // paint empty tiles
		pick,    // take the tile or the region under the cursor
		pan,     // drag the view instead of the map
	};

	// What the canvas is editing, derived from the selected layer: a tile layer
	// gets the tile brush, a quad layer the point handles, and a layer that
	// cannot be edited at all gets nothing. The panels branch on this, so the
	// interface only ever offers what the selection can actually do.
	enum class edit_mode
	{
		none,
		tiles,
		quads,
	};

	struct CLayerView
	{
		bool m_Visible = true;
		bool m_Locked = false;
	};

	/*
		One cell of the brush stamp: a tile index and the flips and rotation it
		carries. A brush is a stamp of these rather than a single tile, so a
		rectangle of the sheet - or a piece of the map itself - can be painted
		with one click. A 1x1 stamp is the plain tile brush.
	*/
	struct CBrushTile
	{
		int m_Index = 0;
		int m_Flags = 0;
	};

	class CEditor
	{
	public:
		bool Init(engine::CGraphics *pGraphics);
		/*
			The same editor without a window: the panels are never drawn and the
			keyboard and mouse arrive from the caller instead of from SDL. The
			command line, the scripted runs and the agent interface all start
			this way, which is why every operation the interface offers is a
			method here rather than something only the drawing code can do.
		*/
		void InitHeadless();
		bool Headless() const { return m_pGraphics == nullptr; }
		// the input of a headless editor; the window build hands out the one
		// SDL feeds
		engine::CInput &Input();
		const engine::CInput &Input() const;
		// the window size the layout and FitToMap use; a headless editor has a
		// fixed one unless the caller sets another
		base::vec2 WindowSize() const;
		void SetWindowSize(base::vec2 Size);
		// one headless frame: process the queued input, without drawing
		void UpdateHeadless();

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

		// the project: the editable state in one file under the application
		// data directory, which is what the editor opens and saves by default
		CProject &Project() { return m_Project; }
		const CProject &Project() const { return m_Project; }
		bool LoadProject(const std::string &Path);
		bool SaveProject(const std::string &Path);
		// the editor session as a JSON object: the camera, the brush, the
		// selection and the workspace layout. SaveProject and SavePackage share
		// it, so a package written from the editor resumes like a project.
		base::json EditorState() const;
		// the project name, which names the export files
		const std::string &ProjectName() const { return m_Project.m_Meta.m_Name; }
		void SetProjectName(const std::string &Name);

		// the autorules of this project, and the operations the workspace and
		// the CLI both use
		rules::CRuleSet &RuleSet() { return m_RuleSet; }
		const rules::CRuleSet &RuleSet() const { return m_RuleSet; }
		bool LoadRules(const std::string &Path);
		bool SaveRules(const std::string &Path);
		// applies one configuration (or every one) to a tile layer, with undo.
		// The layer index is the slot numbering of rules.h.
		bool ApplyRules(int ConfigIndex, int TargetSlot, int Seed = 0, std::string *pError = nullptr);
		/*
			The live tiles: a layer that reads another one and is rewritten as
			that source is painted. RefreshLiveTiles covers a rectangle, which is
			what a stroke calls; ApplyLiveTiles does the whole layer, which is
			what the layer panel calls when a mapping or the source changes.
		*/
		void RefreshLiveTiles(int SourceGroup, int SourceLayer, int X0, int Y0, int X1, int Y1);
		void ApplyLiveTiles(int Group, int Layer);

		/*
			The operations the command line and the agent interface need, kept
			here rather than reached through a friend declaration: a friend would
			let a handler change anything, and this list is the contract of what
			an unattended caller may do.
		*/
		int SelectedGroupIndex() const { return m_SelectedGroup; }
		// read only queries the administrative layer needs; the editing ones stay
		// private so an unattended caller cannot bend the model behind a panel
		CLayer *Layer(int Group, int LayerIndex) { return LayerAt(Group, LayerIndex); }
		const CLayer *Layer(int Group, int LayerIndex) const { return LayerAt(Group, LayerIndex); }
		int SlotOfLayer(int Group, int LayerIndex) const { return SlotForLayer(Group, LayerIndex); }
		bool GameEntities() const { return GameEntitiesAllowed(); }
		int ColorEnvelopeOfSelection() const { return SelectedColorEnvelope(); }
		int PositionEnvelopeOfSelection() const { return SelectedPositionEnvelope(); }
		void AddPhysicsLayerOp(int Flags) { AddPhysicsLayer(Flags); }

		// The rule workspace operations and the envelope list are public because
		// the administrative interface is a first class way to drive this editor:
		// an agent has to be able to build a rule set and a curve, not only
		// paint tiles.
		int RuleConfig() const { return m_RuleConfig; }
		// the run a rule operation adds to or edits: AddRule() clamps it, so a
		// caller uses this rather than assuming the first run
		int RuleRun() const { return m_RuleRun; }
		int RuleIndexInRun() const { return m_RuleIndex; }
		/*
			Which level of the tree the selection is on. The delete button acts
			on it, so a configuration or a run can be removed and not only the
			index rule inside one; before this the button always meant "the rule"
			and there was no way to drop the other two.
		*/
		enum class rule_level { config = 0, run = 1, rule = 2 };
		int RuleLevel() const { return m_RuleLevel; }
		void NewRuleSetOp();
		void AddRuleConfigOp() { AddRuleConfig(); }
		void AddRuleRunOp() { AddRuleRun(); }
		void AddRuleOp() { AddRule(); }
		void DeleteRuleSelectionOp() { DeleteRuleSelection(); }
		// what delete can act on right now, and the level it would name: the
		// tree row the user clicked decides it, and the agent passes it too
		bool CanDeleteRuleSelection() const;
		const char *RuleLevelName() const;
		void SelectRule(int Config, int Run, int Rule, int Level = 2) { SetRuleSelection(Config, Run, Rule, Level); }
		int AddEnvelopeOp(int Channels) { return AddEnvelope(Channels); }
		void AddEnvelopePointOp(float Time) { AddEnvelopePoint(Time); }
		int EnvelopePointOfSelection() const { return m_SelectedEnvelopePoint; }
		// the seed the last autorules run actually used, which differs from the
		// one that was passed when that one was 0
		int LastRuleSeed() const { return m_LastRuleSeed; }
		void SetSelectedEnvelope(int Index) { m_SelectedEnvelope = Index; }
		void SetSelectedEnvelopePoint(int Index) { m_SelectedEnvelopePoint = Index; }
		int SelectedLayerIndex() const { return m_SelectedLayer; }
		/*
			Whether the layer tree's group row is the selection rather than one
			of its layers. A group has no canvas, so the right panel answers it
			with the group's own properties - parallax, offset and the clip
			rectangle - which is the only place those are editable.
		*/
		bool GroupSelected() const { return m_SelectedLayer < 0; }
		int BrushTileIndex() const { return m_BrushTile; }
		int BrushFlagsValue() const { return m_BrushFlags; }
		float Zoom() const { return m_View.m_Zoom; }
		int RuleRunIndex() const { return m_RuleRun; }
		bool AllowExtensions() const { return m_bAllowExtensions; }
		void SetAllowExtensions(bool Allow)
		{
			m_bAllowExtensions = Allow;
			// the model carries the switch too, so an export can warn about what
			// a target cannot express instead of dropping it silently
			m_Map.m_bAllowExtensions = Allow;
		}
		// tells the editor that the model changed underneath it
		void MarkDirty() { m_Dirty = true; }
		// closes the sound device, so nothing outlives the editor that owns it
		~CEditor();
		// the state a caller has to refresh after adding or removing a layer
		void RefreshAfterStructuralEdit()
		{
			m_vViewStates.clear();
			EnsureViewStates();
			ClampSelection();
			m_Dirty = true;
		}
		void AddGroupOp(const std::string &Name);
		// the undo scope is private, so these three are declared here and
		// defined beside the undo machinery
		void PushUndoOpWhole(int Group, int Layer);
		void PushUndoOpTiles(int Group, int Layer);
		/*
			A group's own properties are not a layer's, and the selection may be
			the group row with no layer at all, so this captures the map the way
			the whole scope does without needing a layer index.
		*/
		void PushUndoGroupProperties(int Group);
		void UndoOp() { Undo(); }
		void RedoOp() { Redo(); }
		bool CanUndo() const { return !m_vUndo.empty(); }
		bool CanRedo() const { return !m_vRedo.empty(); }
		void SetBrushFlags(int Flags) { m_BrushFlags = Flags; }
		void AddQuadSelection(int Index);

		/*
			The asset operations: the images and sounds a map carries. They are
			the part of the DDRaceNetwork editor that a map maker uses constantly
			- dropping in a new tileset, embedding a sound - and they are also
			the part an agent cannot fake, because nothing else in the model can
			produce an image.
		*/
		// the index a new image or sound would get, and whether a name is free
		int ImageCount() const { return static_cast<int>(m_Map.m_vImages.size()); }
		int SoundCount() const { return static_cast<int>(m_Map.m_vSounds.size()); }
		int FindImage(std::string_view Name) const;
		int FindSound(std::string_view Name) const;
		// an image or sound that no layer references, or -1
		int UnusedImage() const;
		int UnusedSound() const;
		// how many layers or sources point at an entry
		int ImageReferenceCount(int Index) const;
		int SoundReferenceCount(int Index) const;
		// adds a PNG file from disk; the tile flags are analysed for a mapres
		int AddImageFromFile(const std::string &Path, std::string *pError = nullptr);
		// adds raw RGBA, which is what an embedded image is
		int AddImageRGBA(std::string_view Name, int Width, int Height, byte_vector Pixels, std::string *pError = nullptr);
		// replaces the pixels of an existing image, keeping its index
		bool ReplaceImageFromFile(int Index, const std::string &Path, std::string *pError = nullptr);
		// the same, for pixels that were already decoded (editor_assets.cpp)
		bool ReplaceImagePixels(int Index, int Width, int Height, byte_vector Pixels, std::string *pError = nullptr);
		// embeds an external image (reads its mapres file) or frees it again
		bool EmbedImage(int Index, std::string *pError = nullptr);
		bool ExternaliseImage(int Index, std::string *pError = nullptr);
		bool SetImageName(int Index, std::string_view Name);
		// writes the image out as a .png; an external image is written from its
		// mapres file, an embedded one from its pixels
		bool ExportImage(int Index, const std::string &Path, std::string *pError = nullptr);
		// removes an image and renumbers every reference in the map
		bool RemoveImage(int Index, std::string *pError = nullptr);
		// installs an image from the version's mapres directory by name
		int AddImageFromMapres(std::string_view Name, std::string *pError = nullptr);

		int AddSoundFromFile(const std::string &Path, std::string *pError = nullptr);
		int AddSoundData(std::string_view Name, byte_vector Opus, bool bEmbedded, std::string *pError = nullptr);
		bool SetSoundName(int Index, std::string_view Name);
		bool EmbedSound(int Index, std::string *pError = nullptr);
		bool ExternaliseSound(int Index, std::string *pError = nullptr);
		bool ExportSound(int Index, const std::string &Path, std::string *pError = nullptr);
		bool RemoveSound(int Index, std::string *pError = nullptr);
		int AddSoundFromResource(std::string_view Name, std::string *pError = nullptr);
		// the length of a sound in seconds, decoded from its opus header, so a
		// sound source can be placed without playing it; 0 when unknown
		float SoundLength(int Index) const;

		// a sound layer and its sources, which is what the sound workspace edits
		void AddSoundLayerOp(int SoundId = -1);
		int SoundSourceCount() const;
		// the sound resource the selected sound layer plays, or -1
		int SoundLayerIndex() const;
		int AddSoundSource(float X, float Y);
		bool RemoveSoundSource(int Source);
		bool SetSoundLayerSound(int SoundId);

		// chooses a tool, remembering what the eraser toggle returns to
		void SetTool(editor_tool Tool);
		// E: the eraser, or the tool that was in use before it
		void ToggleEraser();

		// the asset browser: a platform dialog for an image or a sound
		bool ImportImageDialog();
		bool ImportSoundDialog();
		bool ExportAssetDialog(bool bSound);

		// the asset browser: the images and sounds of the map (editor_assets_draw.cpp)
		void DrawAssetsPanel(engine::painter::CPainter *pPainter, const base::rect &Area);
		int AssetThumbnail(int ImageIndex);

		// the toolbox on the far left, and the panels that float over the canvas
		void DrawToolBox(engine::painter::CPainter *pPainter, const base::rect &Area);
		void DrawWelcomePanel(engine::painter::CPainter *pPainter, const base::rect &Canvas);
		void DrawShortcutsPanel(engine::painter::CPainter *pPainter, const base::rect &Canvas);
		// the rect a floating panel gets, centred over the canvas
		base::rect FloatingRect(const base::rect &Canvas, base::vec2 Size) const;

		/*
			The hint that names whatever the cursor is over. A panel requests
			one while it draws and Render() flushes it after everything else,
			so a tooltip can never be covered by the panel that asked for it.
		*/
		void RequestTooltip(base::vec2 Position, const std::string &Title, const std::string &Detail);
		void DrawTooltip(engine::painter::CPainter *pPainter, const base::vec2 &Window);
		/*
			The hint of the widget under the cursor, in one call: the status bar
			gets the detail and the tooltip opens under the widget. Every panel
			labels its buttons with this, which is what makes the interface
			explainable without a manual.
		*/
		void Hint(const base::rect &Area, const std::string &Title, const std::string &Detail);

		// the sound preview, when the build has one
		bool PreviewSound(int Index);
		void StopSoundPreview();
		bool SoundPreviewPlaying() const;
		std::string SoundPreviewName() const;
		void InitSoundPreview();
		void ShutdownSoundPreview();

		// the state the project operations reset: a caller that replaced the map
		// outright has to clear the selection and the history
		void NewMapReset()
		{
			m_bSessionSeeded = false;
			m_bMapVersionKnown = false;
			m_vViewStates.clear();
			EnsureViewStates();
			m_SelectedGroup = 0;
			m_SelectedLayer = 0;
			m_vSelectedQuads.clear();
			m_SelectedEnvelope = -1;
			m_SelectedEnvelopePoint = -1;
			m_vUndo.clear();
			m_vRedo.clear();
			SetupVersionForMap();
			ResetTextures();
			FitToMap();
			m_Dirty = false;
		}

		/*
			Opening and saving go through the platform's own dialog rather than
			a list drawn inside the window: a file browser is the one piece of
			an application the operating system always does better, and a
			mapmaker expects their bookmarks and their network shares to be
			there. The dialogs are asynchronous, so these methods start one and
			PollFileDialog() collects the answer on a later frame.
		*/
		bool OpenFileDialog();
		bool SaveFileDialog(bool bAs);
		bool ExportFileDialog(map_target Target);
		// call once per frame; true when a dialog finished this frame
		bool PollFileDialog();

		void SetStatus(const std::string &Message);
		/*
			The same, for something that actually happened: the line is kept in
			the log as well. Hovering a widget only calls SetStatus, because a
			tooltip is not an event.
		*/
		void Notify(const std::string &Message);
		const std::string &Status() const { return m_Status; }
		bool Dirty() const { return m_Dirty; }
		// whether this session has read a project or a map: an agent command
		// runs in a fresh session, and this is how it knows it may open the
		// project it is about to write rather than overwrite it
		bool SessionSeeded() const { return m_bSessionSeeded; }
		/*
			The mapres version a saved file should record. The target names it -
			`project.save --target ddnet07` has to record ddnet - but a map that
			was opened straight from a .map file knows better: it was built
			against its own tiles and its run length tilemaps say so. A .unitwp
			never counts here, because a package always stores datafile 5 and the
			project's own record is the only thing that knows where it came from.
		*/
		const std::string &VersionForSave() const { return m_bMapVersionKnown ? m_VersionId : m_TargetVersionId; }
		const std::string &SourcePath() const { return m_SourcePath; }
		const CMap &Map() const { return m_Map; }
		CMap &Map() { return m_Map; }
		map_target Target() const { return m_Target; }

		// camera helpers, used by the offscreen screenshot mode
		void FitToMap();
		void SetZoom(float Zoom) { m_View.m_Zoom = Zoom; }
		bool Grid() const { return m_bGrid; }
		void SetGrid(bool Grid) { m_bGrid = Grid; }
		int BrushSize() const { return m_BrushSize; }
		void SetBrushSize(int Size);
		// the brush stamp, so the screenshot mode and the tests can see what
		// Space or the palette selected
		int BrushWidth() const { return m_BrushW; }
		int BrushHeight() const { return m_BrushH; }
		const std::vector<CBrushTile> &Brush() const { return m_vBrush; }
		const std::string &VersionId() const { return m_VersionId; }
		void SetCenter(base::vec2 Center) { m_View.m_Center = Center; }
		void SetTarget(map_target Target);
		void SetSelectedLayer(int Group, int Layer);
		/*
			Renaming a layer or a group, which is what a double click on its row
			in the layer tree starts: the row turns into a name field, Enter or
			a click elsewhere keeps what was typed, Escape leaves the name as it
			was. With Layer < 0 the group itself is renamed.
		*/
		void BeginRename(int Group, int Layer);
		bool Renaming() const { return m_RenameGroup >= 0; }
		int RenameGroup() const { return m_RenameGroup; }
		int RenameLayer() const { return m_RenameLayer; }
		const std::string &RenameText() const { return m_RenameText; }
		bool RenameAllSelected() const { return m_bRenameAll; }
		void CommitRename();
		void CancelRename();
		// reads the keyboard for the open name field; the canvas and the
		// shortcuts are skipped while one is open
		void HandleRenameInput();
		void SetBrushTile(int Index);
		void SetCursor(base::vec2 World) { m_CursorWorld = World; m_bCursorValid = true; }

		// what the canvas edits right now, which follows from the selection:
		// a tile layer gives the tile brush, a quad layer the point handles
		edit_mode Mode() const;
		// the quad selection of a quad layer, also used by the screenshot mode
		void SelectQuad(int Index);
		void ClearQuadSelection();
		// the target that means the four corners at once rather than one of
		// them, which SetQuadColorTarget and QuadColorTarget both carry
		static constexpr int AllQuadCorners = 4;
		void SetQuadColorTarget(int Target);
		int QuadColorTarget() const { return m_QuadColorTarget; }
		const std::vector<int> &SelectedQuads() const { return m_vSelectedQuads; }
		/*
			Colouring a corner from the map, which is what a right click on a
			point of a quad does: the point under the cursor is the corner, so
			the picker opens aimed at it rather than the panel having to be told
			which one is meant. Screen is the canvas position clicked and
			QuadUnderCursor the quad the cursor is inside (-1 for none), which
			widens the target to the four corners of that quad. A click on the
			pivot colours nothing, and says so.
		*/
		bool OpenQuadColorPickerAt(base::vec2 Screen, int QuadUnderCursor);
		// whether the picker popup is up, so a scripted run can see that the
		// right click landed
		bool ColorPopupShown() const { return m_bShowColor; }
		/*
			Where the picker popup is drawn. Opened from the map it sits beside
			the point it was opened from - the corner the right click landed on,
			which stays visible - and it flips to the other side of that point
			when it would leave the canvas, so a corner that is itself in a
			corner of the window still gets a popup next to it. Without a point
			(C, or the button in the panel) it is centred.
		*/
		base::rect ColorPopupRect() const;
		/*
			Where a point of a quad is drawn, in window pixels. The canvas hit
			tests in screen space, so anything that wants to click a corner - the
			offscreen script of a screenshot, or `--selftest` - has to ask where
			that corner is instead of carrying a coordinate the layout would
			move. Point 4 is the pivot; the result is negative when there is no
			such point.
		*/
		base::vec2 QuadPointScreen(int Quad, int Point) const;
		// how the interface names a corner, so the panel, the status line and
		// the picker title cannot say different things
		static const char *CornerName(int Corner);

	public:
		/*
			The panels are docks rather than fixed columns: each one has a side,
			a width, whether it is collapsed and which tab of its side is
			showing, and the two splitters between them can be dragged. That is
			the part of a Krita style workspace that changes how the editor is
			used; the theme in engine/ui_theme.h carries the look.
		*/
		/*
			Which menu of the title bar row is open, if any. The menu bar and
			the dropdown it opens are two pieces of drawing that have to agree
			on where each title is, so the ids are part of the interface rather
			than of the private state; the open one is not, because an open menu
			owns the mouse and the keyboard the way a popup does, and a click on
			one of its rows must not also paint the tile under it.
		*/
		enum class menu_id
		{
			none,
			files,
			settings,
		};

		// the menu the title bar row has open, if any. The drawing code owns
		// the state, but it is also what decides whether the canvas and the
		// panels answer the mouse, so it can be read and set from outside: the
		// offscreen mode opens one the way a click on the bar would.
		menu_id Menu() const { return m_Menu; }
		void OpenMenu(menu_id Menu) { m_Menu = Menu; }

		enum class dock_panel
		{
			layers,
			tiles,
			envelopes,
			rules,
			assets,
			count,
		};

		struct CDockState
		{
			float m_Width = DefaultDockWidth;
			bool m_bCollapsed = false;
			bool m_bVisible = true;
		};

		base::rect LeftDockRect() const;
		base::rect RightDockRect() const;
		// the tab strip of a side: the dock that takes the click handles it
		bool DockTab(engine::painter::CPainter *pPainter, const base::rect &Row, dock_panel Panel, bool Active,
			int Count, int Index, bool bDragging = false);
		CDockState &Dock(dock_panel Panel) { return m_aDocks[static_cast<std::size_t>(Panel)]; }
		const CDockState &Dock(dock_panel Panel) const { return m_aDocks[static_cast<std::size_t>(Panel)]; }
		// which workspace a docker is showing, by slot
		dock_panel DockPanel(dock_slot Slot) const { return m_RightDock[Slot == dock_slot::right_upper ? 0 : 1]; }
		void SetDockTab(dock_slot Slot, dock_panel Panel);
		/*
			The panel buttons of the toolbar work the dockers: a workspace that
			is showing folds its docker away, one that is hidden comes back to
			the docker it belongs to. The workspace itself does not move, so a
			tab dragged to the other docker is toggled where the user put it.
		*/
		bool PanelShown(dock_panel Panel) const;
		void TogglePanel(dock_panel Panel);
		// the docker a workspace starts in, which is where a hidden one returns
		static dock_slot HomeSlot(dock_panel Panel);
		// the tool strip on the left, which the window bar can fold away
		bool ToolBoxShown() const { return m_bShowToolBox; }
		void ToggleToolBox() { m_bShowToolBox = !m_bShowToolBox; }
		void HandleDockInput(const base::vec2 &Window);
		void DrawDockHeaders(engine::painter::CPainter *pPainter, const base::vec2 &Window);
		void DrawEnvelopeWorkspace(engine::painter::CPainter *pPainter, const base::rect &Area);
		void DrawRulesWorkspace(engine::painter::CPainter *pPainter, const base::rect &Area);
		// the right dock's tab content, which is the tile palette or one of the
		// two workspaces
		void DrawDockContent(engine::painter::CPainter *pPainter, dock_panel Panel, const base::rect &Area, bool bLower);

	private:
		// undo: the scope says what was touched, so a colour change does not
		// have to snapshot a whole tile layer
		enum class undo_scope
		{
			tiles,
			quads,
			colour,
			// the envelope points of an envelope the editor does not own
			envelope,
			// anything that rewrites more than one layer, such as a rule run:
			// the snapshot is the whole map, which is what makes it undoable at
			// all without a per operation diff
			whole,
		};

		// ------------------------------------------------------------- helpers
		CLayer *SelectedLayer();		const CLayer *SelectedLayer() const;
		// a layer by index, without going through the selection: the rule
		// workspace and the command line address layers by group and index
		CLayer *LayerAt(int Group, int Layer);
		const CLayer *LayerAt(int Group, int Layer) const;
		CGroup *SelectedGroup();
		const CGroup *SelectedGroup() const;
		CLayerView *ViewState(int Group, int Layer);
		bool EnsureViewStates();
		void ClampSelection();
		// the live tiles of the selected layer, drawn in the layer panel
		void DrawLiveTilesSection(engine::painter::CPainter *pPainter, const base::rect &Area, float &y);

		// the canvas inside the window: the layout, the drawing and the hit
		// testing all go through this one function so they cannot drift apart
		base::rect CanvasRect(const base::vec2 &Window) const;
		// the vertical tool strip at the far left; zero wide when it is hidden
		base::rect ToolBoxRect(const base::vec2 &Window) const;
		// the workspaces a dock can show, in the order its tabs are drawn: the
		// array is the order, so reordering a tab is reordering this list
		// where the left column of the layout starts, which is past the toolbox
		float LeftEdge() const;
		// the world rectangle the tile layers cover, which is the map boundary
		base::rect MapBounds() const;
		// The view a group's layers are drawn with, which folds in the group's
		// parallax and offset: the reference maps a group through
		// center * parallax/100 + offset, keeping the viewport size.
		base::view_transform GroupView(const CGroup &Group) const;
		// The same for the group that is being edited, which is what the cursor,
		// the quad handles and the hit tests use. A group that has parallax or
		// an offset draws its layers somewhere other than the plain view, so
		// testing against m_View would disagree with what is on screen.
		base::view_transform SelectionView() const;


		int TextureForImage(int ImageIndex);
		// the same image with every pixel multiplied by Tint, for the layer
		// colours: an image fill cannot modulate a texture's rgb
		int TextureForImage(int ImageIndex, const CColor &Tint);
		bool TintSource(int ImageIndex, std::vector<std::byte> &Pixels, int &Width, int &Height);
		std::string TintSlotKey(std::size_t Slot) const;
		// the entity sheet of the game layer, chosen by the extensions switch;
		// -1 when no sheet is installed
		int EntityTexture();
		// the sheet and cell of a physics layer, which has its own art: returns
		// -1 when the layer has none
		int PhysicsTexture(const CLayer &Layer);
		// decides which mapres directory the loaded map needs; the caller says
		// whether that verdict may also move the project's recorded assets
		bool SetupVersionForMap(bool bMapDecidesVersion = true);

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
		// the handle of the selected quads nearest the cursor, -1 when none is
		// within grabbing distance
		int QuadHandleAt(base::vec2 Screen) const;

		// -------------------------------------------------------------- actions
		void HandleShortcuts();
		void HandleCanvasInput(const base::rect &Canvas);
		void HandleQuadInput(bool bInCanvas);
		void HandleQuadMoveStart();
		void UpdateQuadHover(bool bInCanvas);
		void HandlePopupInput();
		// the mouse half of the picker popup, which is the half a windowless
		// frame can run: a click anywhere else closes it
		void HandleColorPopupInput();
		void OpenBrowser();
		void PaintAt(base::vec2 World);
		// the game layer is painted with entities rather than tiles, so it has
		// its own catalog and its own gate: a DDRace entity is only offered
		// while the extensions switch is on
		bool GameEntitiesAllowed() const;
		// adds every DDRace physics layer the map does not have yet
		void AddPhysicsLayer(int Flags);
		// ---------------------------------------------------------------- brush
		// The brush is a stamp of tiles, one tile until a region is picked.
		// These keep the stamp, the single tile the panel shows and the tile
		// under the cursor in step.
		void SetBrushUniform(int Size);
		void SetBrushRegion(int Width, int Height, const std::vector<CBrushTile> &Tiles, int PrimaryIndex);
		void ToggleBrushFlags(int Flags);
		// the top left cell of the stamp when it is centred on a world
		// position, which is the anchor painting and the preview both use
		base::ivec2 BrushOrigin(base::vec2 World) const;
		// the cell of the stamp, wrapping, so a fill can tile it
		const CBrushTile &BrushAt(int X, int Y) const;
		bool BrushIsUniform(int Size) const;
		// the tile (or the region) under the world position becomes the brush
		void PickBrushAt(base::vec2 World);
		// the rectangle the pick drag has run over so far, clamped to the layer
		bool PickRect(int &X0, int &Y0, int &X1, int &Y1) const;
		void BeginStroke();
		void EndStroke();
		void PushUndo(int Group, int Layer, undo_scope Scope = undo_scope::tiles);
		void Undo();
		void Redo();
		void ResetTextures();
		// drops just the textures of the map's images and their tint copies,
		// which is what an asset edit needs (editor_textures.cpp)
		void ForgetImageTextures();
		// the layer operations behind the panel buttons and the shortcuts
		void AddLayer(bool bQuads);
		void AddGroup();
		void DeleteSelected(bool bWholeGroup);

		// ------------------------------------------------------------- drawing
		void DrawMap(engine::painter::CPainter *pPainter, const base::rect &Canvas);
		void DrawTileLayer(engine::painter::CPainter *pPainter, const CLayer &Layer, bool Selected);
		void DrawQuadLayer(engine::painter::CPainter *pPainter, const CLayer &Layer);
		// the five handles of the selected quads, drawn over the map
		void DrawQuadHandles(engine::painter::CPainter *pPainter);
		void DrawGameLayer(engine::painter::CPainter *pPainter, const base::rect &Canvas, const CLayer &Layer);
		void DrawPhysicsLayer(engine::painter::CPainter *pPainter, const base::rect &Canvas, const CLayer &Layer, int Flags);
		void DrawCursor(engine::painter::CPainter *pPainter, const base::rect &Canvas);
		// the marquee of a pick drag, which becomes the brush when it is let go
		void DrawPickMarquee(engine::painter::CPainter *pPainter);
		void DrawQuadCursor(engine::painter::CPainter *pPainter);
		void DrawLiveMarquee(engine::painter::CPainter *pPainter);
		void DrawToolbar(engine::painter::CPainter *pPainter, const base::rect &Area);
		// the row above it: the menus and the window buttons, which is what a
		// window without a system title bar needs at its top
		void DrawMenuBar(engine::painter::CPainter *pPainter, const base::rect &Area);
		// the open menu itself, drawn over everything else
		void DrawMenus(engine::painter::CPainter *pPainter);
		void DrawLayerPanel(engine::painter::CPainter *pPainter, const base::rect &Area);
		void DrawPalettePanel(engine::painter::CPainter *pPainter, const base::rect &Area);
		void DrawTileSection(engine::painter::CPainter *pPainter, const base::rect &Area, float &y);
		void DrawQuadSection(engine::painter::CPainter *pPainter, const base::rect &Area, float &y);
		/*
			The properties of a group, shown when the layer tree's group row is
			the selection: parallax, offset and the clip rectangle with the
			switch that turns clipping on. These are the values a group is drawn
			through, and until this panel existed only the command line and the
			agent interface could set them.
		*/
		void DrawGroupSection(engine::painter::CPainter *pPainter, const base::rect &Area, float &y);
		void DrawStatusBar(engine::painter::CPainter *pPainter, const base::rect &Area);
		void DrawPopups(engine::painter::CPainter *pPainter);
		void DrawOpenPopup(engine::painter::CPainter *pPainter);
		void DrawSavePopup(engine::painter::CPainter *pPainter);
		void DrawExportPopup(engine::painter::CPainter *pPainter);
		void DrawColorPopup(engine::painter::CPainter *pPainter);
		void DrawEnvelopeBar(engine::painter::CPainter *pPainter, const base::rect &Area);

		// ------------------------------------------------- the envelope workspace
		/*
			The envelope editor is a timeline rather than a bar: a curve view of
			the envelope the selected layer or quad uses, a list of its points,
			and the four channel values of the selected point. It is its own
			window in the dock layout, so it can be closed, moved and made tall.
		*/
		CEnvelope *SelectedEnvelope();
		const CEnvelope *SelectedEnvelope() const;
		// the envelope index the selection points at, -1 when nothing does
		int SelectedEnvelopeIndex() const;
		// the envelope of the selected layer, or of the first selected quad
		int SelectedColorEnvelope() const;
		int SelectedPositionEnvelope() const;
		// which channel pair the curve view plots: 0 for the first, 2 for the third
		int EnvelopeChannelBase() const { return m_EnvelopeChannelBase; }
		void SetEnvelopeChannelBase(int Base) { m_EnvelopeChannelBase = Base == 2 ? 2 : 0; }
		// moves the selected point's time, clamped between its neighbours
		void SetEnvelopePointTime(int PointIndex, int Time);
		void SetEnvelopePointValue(int PointIndex, int Channel, int Value);
		void AddEnvelopePoint(float Time);
		void DeleteEnvelopePoint(int PointIndex);
		// the envelope of the selected layer or quad, or a new one when there is
		// none yet; returns its index
		int EnsureEnvelopeSelected();
		int AddEnvelope(int Channels);

		// --------------------------------------------------- the rules workspace
		// which rule the workspace is editing, and whether applying it changed
		// the map
		int RuleConfigIndex() const { return m_RuleConfig; }
		int RuleIndex() const { return m_RuleIndex; }
		void SetRuleSelection(int Config, int Run, int Rule, int Level = 2);
		// keeps the three indices, and the level, pointing at something real
		void ClampRuleSelection();
		// a fresh rule set, and a fresh rule with the layer's own tiles as the
		// source
		void NewRuleSet();
		void AddRule();
		void AddRuleConfig();
		void AddRuleRun();
		void DeleteRuleSelection();
		/*
			The visual condition editor of the rule workspace. A position rule is
			found by the offset it tests, so the preview grid can add and remove
			one by clicking a cell; Index is the position rule inside the selected
			index rule.
		*/
		rules::CIndexRule *SelectedIndexRule();
		void AddRuleCondition(int X, int Y);
		void DeleteRuleCondition(int Index);
		void ToggleRuleConditionMode(int Index);
		// the slot numbering of rules.h for a group/layer pair, and back
		int SlotForLayer(int Group, int Layer) const;
		bool LayerForSlot(int Slot, int &Group, int &Layer) const;

		// ---------------------------------------------------------------- state
		engine::CGraphics *m_pGraphics = nullptr;
		// the input of a headless editor; the window build borrows SDL's
		engine::CInput m_HeadlessInput;
		base::vec2 m_WindowSize{1600.0f, 900.0f};

		CMap m_Map;
		CUnitwpPackage m_Package;
		bool m_bPackageLoaded = false;
		// the project this editor is working on, which is what Ctrl+S writes and
		// what names the exported maps
		CProject m_Project;
		// the autorules document of the project
		rules::CRuleSet m_RuleSet;
		// whether the DDRace extensions may be used outside a DDRace target
		bool m_bAllowExtensions = false;
		std::string m_SourcePath;
		// true once a project or a map has been read into this session, so a
		// save can tell "carry on with what is on disk" from "write what I have"
		bool m_bSessionSeeded = false;
		// the sound preview, opened with the editor rather than at start up so
		// a run that never plays a sound never takes an audio device
		bool m_bAudioInit = false;
		/*
			Where the editor and the agent interface disagree about this file is
			the window, not the audio: the member has to exist in both builds so
			that every translation unit sees the same CEditor, and a build that
			does not link the player simply leaves it unused. Gating it on
			UNITEED_AUDIO - a define CMake only sets for the target, not for
			every unit that includes this header - is what made a TU compiled
			without it compute a different offset for everything after it, which
			crashed the moment two differently compiled objects met.
		*/
#if UNITEED_GUI
		engine::CAudio m_Audio;
		/*
			The SVG icons of the chrome, rasterized at the size the display
			asks for. It lives here rather than in the drawing code because the
			editor is what knows when the graphics exist and when they go away.
		*/
		engine::CVectorTextures m_VectorTextures;
#endif
		std::string m_VersionId = "07";
		// the mapres version the current target implies, which a save records
		// unless a bare .map file was opened and its layers say otherwise
		std::string m_TargetVersionId = "07";
		// true only when a plain .map was read into this session
		bool m_bMapVersionKnown = false;
		map_target m_Target = map_target::tee08;
		bool m_Dirty = false;

		base::view_transform m_View;
		base::view_transform m_LayerView;
		bool m_bGrid = true;
		bool m_bShowDetail = true;
		// the toolbox is its own piece of the chrome, so it can be hidden for a
		// canvas that wants every pixel without losing the setting
		bool m_bShowToolBox = true;
		// the welcome page is shown once, until a map is opened or it is closed
		bool m_bShowWelcome = true;
		// the shortcut list floats over the canvas rather than living in a dock
		bool m_bShowShortcuts = false;

		int m_SelectedGroup = 0;
		int m_SelectedLayer = 0;
		std::vector<std::vector<CLayerView>> m_vViewStates;

		editor_tool m_Tool = editor_tool::paint;
		/*
			The tool the eraser toggle goes back to. Krita's eraser key is a
			mode, not a tool: E drops into the eraser, E again returns to
			whatever was being used, and every other way of choosing a tool
			updates what "back" means.
		*/
		editor_tool m_ToolBeforeErase = editor_tool::paint;
		// the tile and flags the panel shows and Q/E/H/J/K edit, the uniform
		// size the size buttons remember, and the stamp the canvas paints
		int m_BrushTile = 1;
		int m_BrushFlags = 0;
		int m_BrushSize = 1;
		int m_BrushW = 1;
		int m_BrushH = 1;
		std::vector<CBrushTile> m_vBrush;
		bool m_bPainting = false;
		base::ivec2 m_StrokeLastTile{-1, -1};
		// a pick drag on the map selects a region of tiles instead of one
		bool m_bPickDragging = false;
		base::ivec2 m_PickStartTile{-1, -1};
		base::ivec2 m_PickEndTile{-1, -1};
		// a Shift drag on a live layer takes the source tiles it frames
		bool m_bLivePicking = false;
		base::ivec2 m_LivePickStartTile{-1, -1};
		base::ivec2 m_LivePickEndTile{-1, -1};
		// the live mapping row the layer panel is editing, and the field it is
		// scrubbing, so one drag is one edit
		int m_LiveMap = -1;
		int m_DragLiveField = -1;
		// the same for the palette: a drag over the sheet takes a rectangle
		bool m_bPaletteSelecting = false;
		base::ivec2 m_PaletteStartCell{-1, -1};
		base::ivec2 m_PaletteEndCell{-1, -1};

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
		// the corner whose colour the picker is editing: 0..3, or all four
		int m_QuadColorTarget = AllQuadCorners;
		// and the handle that is being dragged: the quad it belongs to, the
		// point index (4 is the pivot) and whether the drag has moved it yet
		int m_DragQuad = -1;
		int m_DragPoint = -1;
		bool m_bQuadDragged = false;
		/*
			A plain drag moves the selected quads rather than a point, so the
			press records where every point of the selection started: the drag
			then writes start + delta, which cannot accumulate rounding as the
			mouse moves. Shift is what turns a press on a handle into a point
			drag.
		*/
		struct CQuadMoveStart
		{
			int m_Quad = -1;
			int m_aPoints[5][2] = {};
		};
		bool m_bQuadMoving = false;
		bool m_bQuadMoved = false;
		bool m_bQuadMoveUndoPushed = false;
		base::vec2 m_QuadMoveAnchor;
		std::vector<CQuadMoveStart> m_vQuadMove;

		// the colour picker popup
		bool m_bShowColor = false;
		// where it was opened from, so the popup can sit beside the corner that
		// was clicked rather than over it; negative means centred
		base::vec2 m_PickerAnchor{-1.0f, -1.0f};
		CColor m_PickerColor;
		// the hue survives a trip through grey, which the rgb colour cannot store
		float m_PickerHue = 0.0f;
		bool m_bPickerDragging = false;

		// the palette of the active image, if any
		float m_PaletteScroll = 0.0f;
		int m_PaletteHover = -1;
		float m_LayerScroll = 0.0f;

		/*
			The row of the layer tree whose name is being typed. A layer is
			(group, layer >= 0) and a group is (group, -1); Group is -1 when
			nothing is being renamed. It is editor state rather than a local of
			the drawing code because the name field keeps the keyboard while it
			is open, and a headless run has to be able to drive it.
		*/
		int m_RenameGroup = -1;
		int m_RenameLayer = -1;
		std::string m_RenameText;
		// the name that was there is selected, so the first letter typed or the
		// first backspace replaces it instead of being appended to it
		bool m_bRenameAll = true;

		/*
			The dock layout: which workspace each of the two stacked dockers
			shows, how wide the column is and how the column is split between
			them. The upper docker is Krita's "advanced" area, the lower one is
			where the layer tree lives.
		*/
		std::array<dock_panel, 2> m_RightDock{dock_panel::tiles, dock_panel::layers};
		std::array<CDockState, static_cast<std::size_t>(dock_panel::count)> m_aDocks{};
		bool m_bUpperCollapsed = false;
		bool m_bLowerCollapsed = false;
		// the height the lower docker takes out of the column
		float m_RightSplit = 300.0f;
		// the splitter being dragged: -1 none, 0 the column edge, 1 the split
		int m_DragSplitter = -1;
		float m_DragSplitterStart = 0.0f;
		float m_DragSplitterOrigin = 0.0f;
		// the cursor is over a splitter, so the pointer should say so
		bool m_bSplitterHover = false;
		bool m_bSplitterHoverH = false;

		/*
			Which group property field is being scrubbed, or -1. The value has
			to keep following the mouse while the drag is live, including
			outside the field, so the drag belongs to the editor rather than to
			the widget that started it.
		*/
		int m_DragGroupField = -1;

		// the envelope workspace
		int m_EnvelopeChannelBase = 0;
		int m_SelectedEnvelope = -1;
		int m_SelectedEnvelopePoint = -1;
		float m_EnvelopeZoom = 1.0f;
		float m_EnvelopeScroll = 0.0f;
		bool m_bEnvelopeDragging = false;

		// the rules workspace
		int m_RuleConfig = 0;
		int m_RuleRun = 0;
		int m_RuleIndex = 0;
		// 0 a configuration, 1 a run, 2 an index rule: what delete removes and
		// what the tree draws as the exact selection
		int m_RuleLevel = static_cast<int>(rule_level::rule);
		// the position rule the preview grid has selected, or -1 for none
		int m_RuleCondition = -1;
		// the preview shows the layer's tile sheet instead of the grid
		bool m_bRuleSheet = false;
		// the rare fields (name, flags, randomness, the run and the region) are
		// folded away until a mapmaker asks for them
		bool m_bRuleAdvanced = false;
		// the field a rule IntField is scrubbing, so one drag is one edit
		int m_DragRuleField = -1;
		// the seed of the last apply, reported so a random run can be repeated
		int m_LastRuleSeed = 0;
		// the inspector and the tree scroll apart, so a long inspector can be
		// walked while the tree stays where it is
		float m_RuleScroll = 0.0f;
		float m_RuleTreeScroll = 0.0f;

	public:
		/*
			The dock layout, as rectangles. A Krita style workspace has to be
			describable before it is drawn, because the canvas, the hit testing
			and the splitters all have to agree on where a panel is.
		*/
		base::rect DockArea(dock_slot Slot, const base::vec2 &Window) const;
		// the tab strip at the top of a dock; the content starts below it
		base::rect DockTabRow(dock_slot Slot, const base::vec2 &Window) const;
		base::rect DockContent(dock_slot Slot, const base::vec2 &Window) const;
		// the splitter between the dock column and the canvas, dragged sideways
		base::rect DockSplitter(dock_slot Slot, const base::vec2 &Window) const;
		// the splitter between the two stacked docks, dragged up and down
		base::rect DockSplitterH(const base::vec2 &Window) const;
		// the width the dock column takes in the layout, collapsed or not
		float DockWidth() const;
		bool DockCollapsed(dock_slot Slot) const;
		void SetDockCollapsed(dock_slot Slot, bool bCollapsed);
		/*
			The workspaces a dock shows, in the order its tabs are drawn: the
			list *is* the order, so reordering a tab is reordering it, and the
			order is saved with the project.
		*/
		static const std::vector<dock_panel> &DefaultTabs(dock_slot Slot);
		std::vector<dock_panel> &Tabs(dock_slot Slot);
		const std::vector<dock_panel> &Tabs(dock_slot Slot) const;
		// the label of a workspace, for its tab, and the line that says what
		// the workspace is for when the tab is hovered
		static const char *TabLabel(dock_panel Panel);
		static const char *TabHint(dock_panel Panel);

	private:
		// the platform file dialogs; the enum says what the answer is for
#if UNITEED_GUI
		engine::CFileDialog m_FileDialog;
#endif
		enum class file_dialog_purpose
		{
			none,
			open,
			save,
			save_as,
			export_map,
			// the asset browser: one purpose per asset kind, so PollFileDialog
			// knows what to do with the file the platform dialog returned
			import_image,
			import_sound,
			export_image,
			export_sound,
		};
		file_dialog_purpose m_FileDialogPurpose = file_dialog_purpose::none;
		map_target m_FileDialogTarget = map_target::tee08;
		std::string m_FileDialogDefaultName;
		// which asset an export or a replace is about
		int m_FileDialogAsset = -1;
		// the asset the browser has selected, one per kind
		int m_SelectedImage = -1;
		int m_SelectedSound = -1;
		float m_AssetScroll = 0.0f;

		/*
			The tab order of each dock, and the tab that is being dragged. A tab
			drag is followed here rather than in the drawing code so that the
			order is a piece of editor state, which is what lets it travel with
			the project.
		*/
		std::vector<dock_panel> m_vRightTabs;
		std::vector<dock_panel> m_vLowerTabs;
		int m_DragTab = -1;      // 0 left, 1 right
		int m_DragTabIndex = -1; // the slot that is following the cursor
		base::vec2 m_DragTabPos;
		// a press that never moved is a tab selection, not a reorder
		bool m_bDraggedTabMoved = false;

		// popups
		bool m_bShowOpen = false;
		bool m_bShowSave = false;
		bool m_bShowExport = false;
		bool m_bShowHelp = false;
		menu_id m_Menu = menu_id::none;
		std::string m_TextInput;
		std::vector<std::string> m_vBrowserEntries;
		int m_BrowserSelected = 0;
		float m_BrowserScroll = 0.0f;
		int m_ExportTarget = 3; // index into AllTargets plus the package entry

		/*
			An undo step. The narrow scopes carry only what they touched, which
			is what makes a colour change cheap; the `whole` and `envelope`
			scopes carry the entire map, because a rule run, a layer add or a
			curve edit can rewrite anything and a partial snapshot would restore
			a state that never existed.
		*/
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
			// only for the whole map scopes
			std::vector<CEnvelope> m_vEnvelopes;
			std::vector<CGroup> m_vGroups;
			std::vector<CImage> m_vImages;
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

		// the vanilla and the DDRace entity sheets, cached per slot
		int m_EntityTexture[2] = {-1, -1};
		// the physics layer sheets: front, tele, speedup, switch, tune
		int m_PhysicsTexture[5] = {-1, -1, -1, -1, -1};

		std::string m_Status;
		double m_StatusTime = 0.0;
		// the pending tooltip, if the cursor was over something that has one
		std::string m_TooltipTitle;
		std::string m_TooltipDetail;
		base::vec2 m_TooltipPos;
		bool m_bTooltip = false;
	};
} // namespace game

#endif // GAME_EDITOR_EDITOR_H
