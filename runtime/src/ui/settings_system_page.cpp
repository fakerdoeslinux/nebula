// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_system_page.h"
#include <cstdio>

namespace galaxy::ui {
namespace {
const char* exposed_text(const render::RenderFeatureStatus& status) {
    return status.settings_exposed ? "READY" : "HIDDEN";
}

}  // namespace
std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post, void (*record_diagnostics)(),
                                     const GpuTimingView* timing, const GpuPickerView* gpus, const UpdatesView* updates) {
    std::vector<SettingsRow> rows;
    {
        const auto shown_name = [](const std::string& text) {
            std::string name = mods_ui_text(text, 32u);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            return name;
        };
        const std::string in_use = shown_name(caps.adapter_description.data());
        if (gpus != nullptr && gpus->choices.size() >= 2u && gpus->choose) {
            // AUTO, then each GPU; the choice applies at the next start, so the GPU in use has its own row.
            unsigned at = 0u;
            for (std::size_t i = 0u; i < gpus->choices.size(); ++i) {
                if (!gpus->next.empty() && gpus->choices[i].identity == gpus->next) at = static_cast<unsigned>(i + 1u);
            }
            const std::vector<render::GpuChoice> choices = gpus->choices;
            const std::function<void(const std::string&)> choose = gpus->choose;
            SettingsRow row{"GRAPHICS CARD", at == 0u ? std::string("AUTO") : shown_name(choices[at - 1u].name),
                            [choices, choose, at](RenderConfig&, int delta) {
                                const unsigned next = settings_step(at, static_cast<unsigned>(choices.size() + 1u), delta);
                                if (next != at) choose(next == 0u ? std::string() : choices[next - 1u].identity);
                            },
                            "AUTO PREFERS A DEDICATED GPU. CHANGES APPLY AT THE NEXT START"};
            row.names = at == 0u ? 0u : 2u;
            rows.push_back(std::move(row));
            SettingsRow used{"IN USE", in_use, nullptr, {}};
            used.names = 2u;
            rows.push_back(std::move(used));
        } else {
            SettingsRow row{"GRAPHICS CARD", in_use, nullptr, {}};
            row.names = 2u;
            rows.push_back(std::move(row));
        }
    }
    char text[64]{};
    if (caps.dxr_raytracing_tier != 0u) {
        std::snprintf(text, sizeof(text), "TIER %u.%u %s", caps.dxr_raytracing_tier / 10u, caps.dxr_raytracing_tier % 10u,
                      exposed_text(caps.dxr));
    } else {
        std::snprintf(text, sizeof(text), "NONE");
    }
    rows.push_back({"RAY TRACING HARDWARE", text, nullptr, "DIRECTX RAYTRACING ON THIS GPU"});
    rows.push_back({"NVIDIA STREAMLINE", exposed_text(caps.streamline), nullptr, "LOADS DLSS FROM THE NVIDIA DOWNLOAD"});
    rows.push_back({"NVIDIA REFLEX", exposed_text(caps.reflex), nullptr, "LOW LATENCY WITH FRAME GENERATION"});
    rows.push_back({"DLSS", exposed_text(caps.dlss_super_resolution), nullptr, "UPSCALING ON NVIDIA RTX GPUS"});
    rows.push_back({"DLSS FRAME GENERATION", exposed_text(caps.dlss_frame_generation), nullptr,
                    "GENERATED FRAMES ON NVIDIA RTX 40 AND 50 GPUS"});
    if (const std::size_t colon = post.find(": "); colon != std::string::npos) post.erase(0, colon + 2u);
    rows.push_back({"POST-PROCESS", post, nullptr, "ANTI-ALIASING, FSR 1 AND SHARPENING"});
    rows.push_back(button_row("DIAGNOSTICS", {"RECORD"},
                              [record_diagnostics](RenderConfig&, unsigned) { if (record_diagnostics) record_diagnostics(); },
                              {}));
    if (updates != nullptr) {
        rows.push_back({"UPDATES", {}, nullptr, {}, true});
        const std::function<void()> toggle = updates->toggle;
        rows.push_back({"CHECK AT START", updates->check_at_start ? "ON" : "OFF",
                        [toggle](RenderConfig&, int delta) { if (delta != 0 && toggle) toggle(); },
                        "CHECKS GITHUB FOR A NEW VERSION WHEN NEBULA STARTS"});
        const std::function<void()> check = updates->check;
        const std::function<void()> install = updates->install;
        const std::function<void()> restart = updates->restart;
        const auto check_row = [&rows, check](std::string help) {
            rows.push_back(button_row("CHECK FOR UPDATES", {"CHECK"},
                                      [check](RenderConfig&, unsigned) { if (check) check(); }, std::move(help)));
        };
        const auto version_row = [&rows, updates](std::string help) {
            SettingsRow row{"NEW VERSION", updates->version, nullptr, std::move(help)};
            row.names = 2u;
            rows.push_back(std::move(row));
        };
        switch (updates->step) {
        case UpdateStep::NotChecked: check_row("CHECKS GITHUB FOR A NEW VERSION NOW"); break;
        case UpdateStep::Checking: rows.push_back({"CHECK FOR UPDATES", "CHECKING", nullptr, "CHECKS GITHUB FOR A NEW VERSION NOW"}); break;
        case UpdateStep::UpToDate: check_row("NEBULA IS UP TO DATE"); break;
        case UpdateStep::CheckFailed: check_row("COULD NOT REACH GITHUB"); break;
        case UpdateStep::Available:
            if (!updates->can_install) {
                version_row("INSTALL IT THE SAME WAY YOU INSTALLED NEBULA");
                break;
            }
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"UPDATE"}, [install](RenderConfig&, unsigned) { if (install) install(); },
                                      updates->closes_game ? "CLOSES THE GAME AND INSTALLS THE NEW VERSION"
                                                           : "DOWNLOADS AND INSTALLS THE NEW VERSION"));
            break;
        case UpdateStep::Downloading: {
            version_row({});
            char value[16]{};
            std::snprintf(value, sizeof(value), "%u%%", updates->percent > 100u ? 100u : updates->percent);
            rows.push_back({"UPDATE NEBULA", value, nullptr, "DOWNLOADING"});
            break;
        }
        case UpdateStep::Installing:
            version_row({});
            rows.push_back({"UPDATE NEBULA", "INSTALLING", nullptr, {}});
            break;
        case UpdateStep::Restart:
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"RESTART"}, [restart](RenderConfig&, unsigned) { if (restart) restart(); },
                                      "THE NEW VERSION RUNS AFTER A RESTART"));
            break;
        case UpdateStep::InstallFailed:
            version_row({});
            rows.push_back(button_row("UPDATE NEBULA", {"TRY AGAIN"}, [install](RenderConfig&, unsigned) { if (install) install(); },
                                      "THE UPDATE COULD NOT BE INSTALLED"));
            break;
        }
    }
    if (timing != nullptr) {
        rows.push_back({"PERFORMANCE", {}, nullptr, {}, true});
        void (*const toggle)() = timing->toggle;
        rows.push_back({"GPU TIMING", timing->on ? "ON" : "OFF",
                        [toggle](RenderConfig&, int) { if (toggle) toggle(); },
                        "MILLISECONDS THE GPU SPENDS ON EACH PASS OF A FRAME. OFF AT EVERY START; WHILE OFF NOTHING IS MEASURED"});
        float total = 0.0f;
        for (const auto& [name, ms] : timing->passes) {
            char value[24]{};
            std::snprintf(value, sizeof(value), "%.2f MS", static_cast<double>(ms));
            rows.push_back({upper_ascii(name), value, nullptr, "AVERAGE OVER THE LAST FRAMES THAT RAN THIS PASS"});
            total += ms;
        }
        if (timing->on && !timing->passes.empty()) {
            char value[24]{};
            std::snprintf(value, sizeof(value), "%.2f MS", static_cast<double>(total));
            rows.push_back({"ALL PASSES", value, nullptr, "THE SUM OF THE PASSES ABOVE"});
        }
    }
    return rows;
}


}  // namespace galaxy::ui
