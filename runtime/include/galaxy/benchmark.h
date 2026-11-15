// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark (Nebula --benchmark 1, docs/using-nebula/command-line-arguments.md): real stages from the player's game data, entered
// through the game's own stage change (the settings' galaxy select path, svc_galaxy_travel), measured from the sources
// the host already has, then a probe of each optional pass and the recommended settings (benchmark_recommend.h), into
// one JSON document in the data folder's diagnostics. It runs on a blank save held in memory, never applies mods, and
// never writes a setting, a save, a counter or an achievement: a crash or a quit at any point leaves the player's files
// as they were.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy::bench {

// True from start() to the end of the process. The settings writers, the save service, the counters and the input read
// test it (one relaxed load).
inline std::atomic<bool> g_running{false};
[[nodiscard]] inline bool running() noexcept {
    return g_running.load(std::memory_order_relaxed);
}

// The render thread at each new picture shown (RenderCore::note_picture_shown), with the frame-tails cadence sample's
// time, and the mixing thread at each mix (AudioMixer::mix): set only while a benchmark runs, so the call sites cost one
// load and link nothing.
inline std::atomic<void (*)(std::uint64_t)> g_picture_hook{nullptr};
inline std::atomic<void (*)()> g_audio_hook{nullptr};
inline void note_new_picture(std::uint64_t steady_ns) noexcept {
    if (void (*hook)(std::uint64_t) = g_picture_hook.load(std::memory_order_acquire)) {
        hook(steady_ns);
    }
}
inline void note_audio_thread() noexcept {
    if (void (*hook)() = g_audio_hook.load(std::memory_order_acquire)) {
        hook();
    }
}

// The player stops the run (Escape, a controller's B, Android's back; native_settings.cpp): any thread. The game ends
// at its next picture; the result is written as stopped (status "aborted", failure "stopped") with the steps that
// finished, and the game starts again with it (returns_home).
inline std::atomic<bool> g_stop_asked{false};
inline void stop() noexcept {
    if (running()) g_stop_asked.store(true, std::memory_order_relaxed);
}
[[nodiscard]] inline bool stopping() noexcept {
    return g_stop_asked.load(std::memory_order_relaxed);
}

// True while a measured window runs: the game's frame clock (svc_frame_wait) and the renderers' present pacer
// (PresentPacer::pace) wait for nothing, so each frame comes as soon as the device has made the one before it (the
// render queue's depth holds the game back). The game's frames are the same on every device; only their speed differs.
inline std::atomic<bool> g_unpaced{false};
[[nodiscard]] inline bool unpaced() noexcept {
    return g_unpaced.load(std::memory_order_relaxed);
}

// The renderers, as one frame's pass timestamps are read back (render thread): each pass's milliseconds in that frame,
// `names[i]` static text. Set only while a window measures.
inline std::atomic<void (*)(const char* const* names, const double* ms, std::size_t count)> g_gpu_frame_hook{nullptr};
inline void note_gpu_frame(const char* const* names, const double* ms, std::size_t count) noexcept {
    if (void (*hook)(const char* const*, const double*, std::size_t) = g_gpu_frame_hook.load(std::memory_order_acquire)) {
        hook(names, ms, count);
    }
}

// The game's disc reads (svc_asset_read), counted while a benchmark runs: a slow frame that read is a loading hitch.
inline std::atomic<std::uint64_t> g_read_bytes{0u};
inline void note_read(std::uint64_t bytes) noexcept {
    if (running()) g_read_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

// The work the game hands the renderer while a benchmark runs: each frame list's draw items (RenderCore::render_frame)
// and the texture images made or rewritten (TextureStore), with their bytes.
inline std::atomic<std::uint64_t> g_draw_items{0u};
inline std::atomic<std::uint64_t> g_texture_uploads{0u};
inline std::atomic<std::uint64_t> g_texture_bytes{0u};
inline void note_frame_list(std::uint32_t items) noexcept {
    if (running()) g_draw_items.fetch_add(items, std::memory_order_relaxed);
}
inline void note_texture(std::uint64_t bytes) noexcept {
    if (running()) {
        g_texture_uploads.fetch_add(1u, std::memory_order_relaxed);
        g_texture_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

struct Options {
    std::filesystem::path pak_folder;  // benchmark.tsv beside game.pak replaces the built-in stage list
    std::filesystem::path out_file;    // --benchmark-out; empty: a new file in results_folder() (benchmark_results.h)
    bool return_home = false;          // --benchmark-return 1: the game starts again after it, with its result open
};

// The run as the game's run panel shows it (live_state).
enum class Part : unsigned { Yours, Standard, Probe, Again };
struct LiveState {
    std::uint32_t step = 0u, steps = 0u;  // the step running, from 1, of the run's steps
    Part part = Part::Yours;              // what the step measures: your settings, the standard settings, one
                                          // optional pass, or the first stage again (the device warmed up)
    bool measuring = false;               // the step's frames are being measured (not loading or settling)
    std::uint32_t fps_tenths = 0u;        // pictures shown a second over the last half second, in tenths; 0 not known
    std::uint32_t progress = 0u;          // the whole run, 0 to 10000, never going back
    bool stopping = false;                // the player stopped it: the game is about to start again
    std::string stage;                    // the step's stage (the game's name: CosmosGardenGalaxy)
};
// Game thread: the run now; false when no benchmark runs or it has ended.
[[nodiscard]] bool live_state(LiveState& out);

// Main thread, before the game module attaches.
void start(const Options& options);

// Game thread, once a scene frame (svc_galaxy_travel): the travel to ask of the scene now (NEBULA_GALAXY_TRAVEL_*), with
// `stage` and `star` filled for one. `current_stage` is the stage the game last reported (svc_game_stage).
[[nodiscard]] int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star);
// Game thread, at each scene's start (svc_presence from GameScene::start; not the file select's): a travel to the
// stage the game is on has arrived when its new scene starts.
void scene_started() noexcept;

// The game's save files while the benchmark runs (svc_save_request): in memory only. `op` is NEBULA_SAVE_READ, _WRITE or
// _DELETE; the result a NEBULA_SAVE_* value (galaxy::saves::Result).
[[nodiscard]] std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size,
                                        std::uint32_t* length) noexcept;

// The host's exit: a run that did not finish writes its result as "aborted".
void at_exit() noexcept;

// The file this run writes, fixed by start(); empty when there is no data folder and no --benchmark-out.
[[nodiscard]] std::filesystem::path result_file();
// Started to return (Options::return_home) and ended (complete, failed or stopped): the host starts the game again.
[[nodiscard]] bool returns_home() noexcept;

}  // namespace galaxy::bench
