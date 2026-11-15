// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {
// The GPU timing rows: the switch (off at every start) and, while it is on, each pass's time.
struct GpuTimingView {
    bool on = false;
    void (*toggle)() = nullptr;
    std::vector<std::pair<std::string, float>> passes;  // name, milliseconds
};

// The GRAPHICS CARD choice: the GPUs the renderer found, the one the next start takes (empty: automatic) and what
// choosing one does. With fewer than two GPUs the row only names the one in use.
struct GpuPickerView {
    std::vector<render::GpuChoice> choices;
    std::string next;
    std::function<void(const std::string&)> choose;
};

// The UPDATES rows (desktop hosts; none where a store updates Nebula): the start's check switch, the check, and the
// install of a newer release.
enum class UpdateStep : unsigned {
    NotChecked,     // no answer this session or kept from an earlier one
    Checking,
    UpToDate,
    CheckFailed,
    Available,      // `version` is newer
    Downloading,    // `percent` of it
    Installing,     // checked, being put in place
    Restart,        // installed: the next start runs it
    InstallFailed,
};
struct UpdatesView {
    bool check_at_start = true;
    UpdateStep step = UpdateStep::NotChecked;
    std::string version;        // Available and after
    unsigned percent = 0u;      // Downloading
    bool can_install = true;    // false: the system installs Nebula's updates (a distribution's package, Flatpak)
    bool closes_game = false;   // Windows: Nebula Setup installs it once the game has closed
    std::function<void()> toggle, check, install, restart;
};

std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post_process,
                                     void (*record_diagnostics)(), const GpuTimingView* timing = nullptr,
                                     const GpuPickerView* gpus = nullptr, const UpdatesView* updates = nullptr);
}  // namespace galaxy::ui
