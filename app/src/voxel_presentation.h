#ifndef FE8_VOXEL_PRESENTATION_H
#define FE8_VOXEL_PRESENTATION_H
#include "fe8_profile.h"

typedef enum Fe8VoxelScene {
    FE8_VOXEL_OFF, FE8_VOXEL_UNSUPPORTED_ROM, FE8_VOXEL_EXTENSIONS_OFF,
    FE8_VOXEL_WAITING_MAP, FE8_VOXEL_INVENTORY, FE8_VOXEL_BUSY,
    FE8_VOXEL_SELECTED, FE8_VOXEL_WAITING_HUD, FE8_VOXEL_READY
} Fe8VoxelScene;

/* Eligibility to refresh live WORLD inputs, not whether to display 3D.
 * main retains verified world inputs and the same camera when this returns a
 * non-ready state, with the intact current native UI in a panel. Never remove
 * this safety oracle or interpret combat VRAM as terrain to maintain 3D. */
static inline Fe8VoxelScene fe8_voxel_scene(bool enabled, bool supported,
        bool extensions, const Fe8Snapshot *snapshot, bool validated_map,
        bool inventory, bool verified_hud) {
    if (!enabled) return FE8_VOXEL_OFF;
    if (!supported) return FE8_VOXEL_UNSUPPORTED_ROM;
    if (!extensions) return FE8_VOXEL_EXTENSIONS_OFF;
    if (inventory) return FE8_VOXEL_INVENTORY;
    if (!snapshot) return FE8_VOXEL_WAITING_MAP;
    if (snapshot->input_lock > 1 || snapshot->phase || snapshot->combat_panel_active ||
            (snapshot->input_lock == 1 && !snapshot->active_unit_address))
        return FE8_VOXEL_BUSY;
    if ((snapshot->game_state_bits & 3) && !snapshot->active_unit_address)
        return FE8_VOXEL_SELECTED;
    if (!validated_map) return FE8_VOXEL_WAITING_MAP;
    if (!verified_hud) return FE8_VOXEL_WAITING_HUD;
    return FE8_VOXEL_READY;
}
static inline const char *fe8_voxel_scene_label(Fe8VoxelScene scene) {
    switch (scene) {
    case FE8_VOXEL_OFF: return "off";
    case FE8_VOXEL_UNSUPPORTED_ROM: return "on - unsupported ROM profile";
    case FE8_VOXEL_EXTENSIONS_OFF: return "on - enable Extended Renderer";
    case FE8_VOXEL_WAITING_MAP: return "on - waiting for a validated tactical map";
    case FE8_VOXEL_INVENTORY: return "on - native inventory";
    case FE8_VOXEL_BUSY: return "on - native menu, movement, or event";
    case FE8_VOXEL_SELECTED: return "on - native unit selection or menu";
    case FE8_VOXEL_WAITING_HUD: return "on - waiting for a supported map HUD";
    case FE8_VOXEL_READY: return "live";
    }
    return "unknown";
}
#endif
