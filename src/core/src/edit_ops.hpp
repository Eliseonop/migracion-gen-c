#pragma once

// Implementación de cada operación (una sobrecarga de `run` por comando).
// La API pública es run_command/apply_command en gc/core/edit.hpp.

#include "gc/core/edit.hpp"

namespace gc::core::detail {

// La pista de vídeo de ARRIBA si está libre en [start, start+dur); si no, una nueva encima
// de todas, cuyo id va a `created` (timeline_ops._top_free_video_track).
std::string top_free_video_track(Timeline& out, double start, double dur, std::vector<std::string>& created,
                                 IdSource& ids);

Result<EditResult> run(const Timeline& tl, const ops::AddTrack& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::ReorderTrack& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::RenameTrack& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::RemoveTrack& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::LinkTracks& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::UnlinkTrack& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::MoveClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::RemoveClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SplitClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::DuplicateClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetProjectFormat& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetMarkers& op, IdSource& ids);

// edit_ops_props.cpp
Result<EditResult> run(const Timeline& tl, const ops::SetClipOpacity& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipSpeed& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipTransition& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetTextRole& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipBlend& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipFlip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipLayout& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipNote& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::UpdateClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::ReframeClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::CropClip& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipKeyframes& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipBeats& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipMasks& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipEffects& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::ReplaceMedia& op, IdSource& ids);

// edit_ops_media.cpp
Result<EditResult> run(const Timeline& tl, const ops::ApplyEffect& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipAnimation& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::FreezeFrame& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddSubtitles& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddShape& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddCinemaBars& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddAdjustmentLayer& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipBgRemoval& op, IdSource& ids);

// edit_ops_compound.cpp
Result<EditResult> run(const Timeline& tl, const ops::PasteClipAttributes& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::FollowObject& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AddSoundDesign& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::AnimateClip& op, IdSource& ids);

// edit_ops_recipes.cpp
Result<EditResult> run(const Timeline& tl, const ops::ApplyRecipe& op, IdSource& ids);

// edit_ops_audio.cpp
Result<EditResult> run(const Timeline& tl, const ops::SetClipVolume& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetTrackAudio& op, IdSource& ids);
Result<EditResult> run(const Timeline& tl, const ops::SetClipAudioFx& op, IdSource& ids);

} // namespace gc::core::detail
