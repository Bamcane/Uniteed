/*
	Sound layers and the sound preview.

	A sound layer names one sound resource and holds a list of *sources*: points
	in world space with a pan, a falloff, a shape and an optional envelope. The
	DDRaceNetwork editor lets a map maker drop sources on the canvas and hear
	what they will sound like, which is the only way to place a sound by ear;
	that needs the layer, the whole source model and a decoder, not just the
	sound resources.

	The preview itself is engine/audio.h, which the headless build leaves out:
	everything else here only touches the map model, so a script can still add
	a sound layer and place sources.
*/

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "base/logger.h"
#include "base/string.h"
#include "game/assets.h"
#include "game/editor/editor.h"

#if UNITEED_GUI && defined(UNITEED_AUDIO)
#include "engine/audio.h"
#endif

namespace game
{
	using base::i18n::Tr;
	CEditor::~CEditor()
	{
		// the audio device belongs to this editor, so it is closed here rather
		// than left to a global destructor
		ShutdownSoundPreview();
#if UNITEED_GUI
		// The SDL textures of the icons are the graphics' to destroy, but this
		// has to let go of them before the graphics do: the cache holds the
		// handle of every one of them.
		m_VectorTextures.Close();
#endif
	}

	void CEditor::AddSoundLayerOp(int SoundId)
	{
		CGroup *pGroup = SelectedGroup();
		if(pGroup == nullptr && !m_Map.m_vGroups.empty())
			pGroup = &m_Map.m_vGroups.front();
		if(pGroup == nullptr)
			return;

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		CLayer Layer;
		Layer.m_Type = LAYERTYPE_SOUNDS;
		Layer.m_Name = "Sounds";
		Layer.m_Flags = 0;
		Layer.m_Sound = SoundId >= 0 && SoundId < SoundCount() ? SoundId : -1;
		pGroup->m_vLayers.push_back(std::move(Layer));
		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedLayer = static_cast<int>(pGroup->m_vLayers.size()) - 1;
		m_Dirty = true;
		const CLayer &Added = pGroup->m_vLayers.back();
		SetStatus(base::Format("added a sound layer ({})", Added.m_Sound >= 0 ?
			m_Map.m_vSounds[static_cast<std::size_t>(Added.m_Sound)].m_Name.c_str() : "no sound chosen"));
	}

	int CEditor::SoundSourceCount() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsSounds())
			return 0;
		return static_cast<int>(pLayer->m_vSources.size());
	}

	int CEditor::SoundLayerIndex() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsSounds())
			return -1;
		return pLayer->m_Sound;
	}

	int CEditor::AddSoundSource(float X, float Y)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsSounds())
			return -1;

		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		CSoundSource Source;
		Source.m_Position.x = X;
		Source.m_Position.y = Y;
		Source.m_Pan = 0;
		Source.m_Loop = 0;
		Source.m_Falloff = 0;
		Source.m_Shape.m_Type = CSoundShape::SHAPE_RECTANGLE;
		// a source with no size is inaudible in the client, so start it at a
		// sane size the way the reference editor does
		Source.m_Shape.m_Width = 400 * 1024;
		Source.m_Shape.m_Height = 400 * 1024;
		pLayer->m_vSources.push_back(Source);
		m_Dirty = true;
		return static_cast<int>(pLayer->m_vSources.size()) - 1;
	}

	bool CEditor::RemoveSoundSource(int Source)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsSounds())
			return false;
		if(Source < 0 || Source >= static_cast<int>(pLayer->m_vSources.size()))
			return false;
		PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::whole);
		pLayer->m_vSources.erase(pLayer->m_vSources.begin() + Source);
		m_Dirty = true;
		return true;
	}

	bool CEditor::SetSoundLayerSound(int SoundId)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsSounds())
			return false;
		if(SoundId < -1 || SoundId >= SoundCount())
			return false;
		pLayer->m_Sound = SoundId;
		m_Dirty = true;
		return true;
	}

	bool CEditor::PreviewSound(int Index)
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		if(Index < 0 || Index >= SoundCount())
			return false;
		const CSound &Sound = m_Map.m_vSounds[static_cast<std::size_t>(Index)];
		if(!Sound.m_Data.empty())
			return m_Audio.Play(Sound.m_Name, Sound.m_Data);
		const std::optional<storage::path> File = FindSoundFile(Sound.m_Name);
		if(!File.has_value())
		{
			SetStatus(base::Format("no audio/{}.opus in this version", Sound.m_Name.c_str()));
			return false;
		}
		return m_Audio.PlayFile(Sound.m_Name, File->string());
#else
		(void)Index;
		SetStatus(Tr("this build has no sound preview"));
		return false;
#endif
	}

	void CEditor::StopSoundPreview()
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		m_Audio.Stop();
#endif
	}

	bool CEditor::SoundPreviewPlaying() const
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		return m_Audio.IsPlaying();
#else
		return false;
#endif
	}

	std::string CEditor::SoundPreviewName() const
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		return std::string(m_Audio.NowPlaying());
#else
		return {};
#endif
	}

	void CEditor::InitSoundPreview()
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		if(m_bAudioInit)
			return;
		m_bAudioInit = true;
		if(!m_Audio.Init())
			LOG_WARN("editor", "no audio device: the sound preview is silent");
#endif
	}

	void CEditor::ShutdownSoundPreview()
	{
#if UNITEED_GUI && defined(UNITEED_AUDIO)
		if(!m_bAudioInit)
			return;
		m_bAudioInit = false;
		m_Audio.Shutdown();
#endif
	}
} // namespace game
