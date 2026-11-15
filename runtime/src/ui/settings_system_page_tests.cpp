#include "galaxy/ui/settings_system_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared System page must not include Windows.h
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int recorded = 0;
void record() { ++recorded; }
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
}

int main() {
    try {
        galaxy::render::RenderFeatureCaps caps;
        std::memcpy(caps.adapter_description.data(), "TEST GPU  ", 11);
        caps.dxr_raytracing_tier = 11;
        caps.dxr.settings_exposed = true;
        auto rows = galaxy::ui::system_page(caps, "POST: SMAA", record);
        check(row(rows, "GRAPHICS CARD").value == "TEST GPU", "adapter description trimmed");
        check(row(rows, "RAY TRACING HARDWARE").value == "TIER 1.1 READY", "feature tier and visibility");
        check(row(rows, "NVIDIA STREAMLINE").value == "HIDDEN", "unexposed feature");
        check(row(rows, "POST-PROCESS").value == "SMAA", "post-process label");
        galaxy::ui::RenderConfig config;
        row(rows, "DIAGNOSTICS").press(config, 0);
        check(recorded == 1, "recording action");
        galaxy::ui::GpuTimingView off;
        off.toggle = [] {};
        auto untimed = galaxy::ui::system_page(caps, "POST: SMAA", record, &off);
        check(row(untimed, "GPU TIMING").value == "OFF", "timing is off by default");
        galaxy::ui::GpuTimingView on;
        on.on = true;
        on.toggle = [] {};
        on.passes = {{"scene", 3.0f}, {"present", 0.5f}};
        auto timed = galaxy::ui::system_page(caps, "POST: SMAA", record, &on);
        check(row(timed, "GPU TIMING").value == "ON" && row(timed, "SCENE").value == "3.00 MS" && row(timed, "ALL PASSES").value == "3.50 MS",
              "passes and their sum are shown while on");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "GPU TIMING"; }), "no timing rows without a view");
        check(!row(rows, "GRAPHICS CARD").change && (row(rows, "GRAPHICS CARD").names & 2u) != 0u,
              "one GPU: the row names it and changes nothing");
        std::string chosen = "unset";
        galaxy::ui::GpuPickerView picker;
        picker.choices = {{"10de:2b85:89ef1043", "NVIDIA GeForce RTX 5090"}, {"8086:7d55:00000000", "Intel(R) Arc(TM) Graphics"}};
        picker.choose = [&chosen](const std::string& identity) { chosen = identity; };
        auto automatic = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        check(row(automatic, "GRAPHICS CARD").value == "AUTO" && (row(automatic, "GRAPHICS CARD").names & 2u) == 0u,
              "the automatic choice shows AUTO, translated");
        check(row(automatic, "IN USE").value == "TEST GPU", "the GPU in use has its own row");
        row(automatic, "GRAPHICS CARD").change(config, -1);
        check(chosen == "unset", "left from AUTO stays at AUTO");
        row(automatic, "GRAPHICS CARD").change(config, 1);
        check(chosen == "10de:2b85:89ef1043", "right from AUTO takes the first GPU");
        picker.next = "8086:7d55:00000000";
        auto second = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        check(row(second, "GRAPHICS CARD").value == "Intel(R) Arc(TM) Graphics" && (row(second, "GRAPHICS CARD").names & 2u) != 0u,
              "a chosen GPU shows its name as is");
        row(second, "GRAPHICS CARD").change(config, -1);
        check(chosen == "10de:2b85:89ef1043", "left from the second GPU takes the first");
        picker.next = "10de:2b85:89ef1043";
        auto first = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        row(first, "GRAPHICS CARD").change(config, -1);
        check(chosen.empty(), "left from the first GPU goes back to AUTO");
        check(std::none_of(first.begin(), first.end(), [](const auto& r) { return r.label == "CHECK AT START"; }),
              "no UPDATES rows where the platform has no updates");
        int toggled = 0, checked = 0, installed = 0, restarted = 0;
        galaxy::ui::UpdatesView updates;
        updates.toggle = [&toggled] { ++toggled; };
        updates.check = [&checked] { ++checked; };
        updates.install = [&installed] { ++installed; };
        updates.restart = [&restarted] { ++restarted; };
        const auto page = [&] { return galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, nullptr, &updates); };
        auto idle = page();
        check(row(idle, "CHECK AT START").value == "ON", "the start's check is on");
        row(idle, "CHECK AT START").change(config, 1);
        check(toggled == 1, "the switch toggles");
        row(idle, "CHECK FOR UPDATES").press(config, 0);
        check(checked == 1, "CHECK checks");
        updates.step = galaxy::ui::UpdateStep::Checking;
        auto checking = page();
        check(row(checking, "CHECK FOR UPDATES").value == "CHECKING" && row(checking, "CHECK FOR UPDATES").buttons.empty(),
              "a running check has no button");
        updates.step = galaxy::ui::UpdateStep::UpToDate;
        auto current = page();
        check(row(current, "CHECK FOR UPDATES").help == "NEBULA IS UP TO DATE", "up to date");
        updates.step = galaxy::ui::UpdateStep::Available;
        updates.version = "2.1.0";
        auto available = page();
        check(row(available, "NEW VERSION").value == "2.1.0" && (row(available, "NEW VERSION").names & 2u) != 0u,
              "the new version is shown as is");
        row(available, "UPDATE NEBULA").press(config, 0);
        check(installed == 1, "UPDATE installs");
        updates.can_install = false;
        auto system_installs = page();
        check(std::none_of(system_installs.begin(), system_installs.end(), [](const auto& r) { return r.label == "UPDATE NEBULA"; }),
              "no UPDATE where the system installs Nebula");
        updates.can_install = true;
        updates.step = galaxy::ui::UpdateStep::Downloading;
        updates.percent = 140u;
        auto downloading = page();
        check(row(downloading, "UPDATE NEBULA").value == "100%", "the download's share, at most 100%");
        updates.step = galaxy::ui::UpdateStep::Restart;
        auto ready = page();
        row(ready, "UPDATE NEBULA").press(config, 0);
        check(restarted == 1, "RESTART restarts");
        updates.step = galaxy::ui::UpdateStep::InstallFailed;
        auto failed = page();
        row(failed, "UPDATE NEBULA").press(config, 0);
        check(installed == 2 && row(failed, "UPDATE NEBULA").buttons.front() == "TRY AGAIN", "a failed install tries again");
        std::cout << "System page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
