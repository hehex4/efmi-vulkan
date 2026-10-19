// Original window, resolution and cursor operands answered from the host's sample.
static void runtime_window_cases(const std::filesystem::path& folder)
{
    using namespace efmi::frontend;
    const auto root = std::filesystem::absolute(folder / "window-operands");
    std::filesystem::create_directories(root);
    const auto ini = root / "window.ini";
    write(ini, R"ini(namespace = window_case
[Constants]
global $window_width = 0
global $window_height = 0
global $res_width = 0
global $res_height = 0
global $cursor_window_x = 0
global $cursor_window_y = 0
global $cursor_screen_x = 0
global $cursor_x = 0
global $cursor_y = 0
global $cursor_visible = 0
global $draw_width = 0
[Present]
$window_width = window_width
$window_height = window_height
$res_width = res_width
$res_height = res_height
$cursor_window_x = cursor_window_x
$cursor_window_y = cursor_window_y
$cursor_screen_x = cursor_screen_x
$cursor_x = cursor_x
$cursor_y = cursor_y
$cursor_visible = cursor_showing
[TextureOverrideWindow]
hash = 12345678
$draw_width = window_width
)ini");
    std::filesystem::remove(root / "settings" / "window.ini");
    RuntimeConfig config{root, {ini}, root / "settings", "window"};
    std::string reason;
    auto runtime = IniRuntime::load(config, {}, reason);
    if (!runtime) throw std::runtime_error(reason);
    check(runtime->status().diagnostics == 0, "Window operand fixture parses without original warnings");
    auto value = [&](const char* name) { return runtime->status().variables.at(std::string("$\\window_case\\") + name); };
    auto present = [&](const RuntimeInput& input) { if (!runtime->present(input, 1, 0.f, reason)) throw std::runtime_error(reason); };

    // No window, cursor or swap chain sample: the original without hWnd and without a
    // resolution reaches the 1920x1080 fallback and reports screen positions as window ones.
    RuntimeInput input; input.foreground = true;
    present(input);
    check(value("window_width") == 1920 && value("window_height") == 1080, "Window size without a window falls back to the original default");
    check(value("res_width") == -1 && value("res_height") == -1, "Resolution stays the original unset value until the host supplies one");
    check(value("cursor_visible") == 0 && value("cursor_window_x") == 0 && value("cursor_x") == 0, "No cursor sample reads as a hidden cursor at the origin");

    input.window = {true, 100, 50, 1600, 900};
    input.cursor = {true, 500, 300};
    input.resolution_width = 3200; input.resolution_height = 1800;
    present(input);
    check(value("window_width") == 1600 && value("window_height") == 900, "Window size is the client rectangle");
    check(value("res_width") == 3200 && value("res_height") == 1800, "Resolution is the swap chain size");
    check(value("cursor_window_x") == 400 && value("cursor_window_y") == 250 && value("cursor_screen_x") == 500,
          "Cursor window position is the screen position less the client origin");
    check(value("cursor_x") == 0.25f && value("cursor_y") == 250.f / 900.f && value("cursor_visible") == 1,
          "Relative cursor divides by the original window heuristic");
    RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30; draw.frame = 1;
    efmi::assets::FilePlan plan;
    check(runtime->draw(draw, plan, reason) && value("draw_width") == 1600, "Draws read the latest present sample");

    // The heuristic takes the resolution when the client is narrower than 640 pixels.
    input.window = {true, 0, 0, 600, 400};
    input.cursor.showing = false;
    present(input);
    check(value("window_width") == 3200 && value("window_height") == 1800, "Narrow window falls back to the resolution");
    check(value("cursor_visible") == 0 && value("cursor_window_x") == 500, "Hidden cursor keeps its position");

    // A window without a resolution keeps the earlier resolution, as the original does.
    input.resolution_width = 0; input.resolution_height = 0;
    present(input);
    check(value("res_width") == 3200, "An absent resolution keeps the previous swap chain size");
    input.resolution_width = 1; input.resolution_height = 0;
    check(!runtime->present(input, 1, 0.f, reason), "A half resolution sample is refused");
}
// Original operands the host answers outside the window sample: fps from the original FPS counter
// advanced by the host clock, effective_dpi and the cursor hotspot from the host's own sample.
static void runtime_host_value_cases(const std::filesystem::path& folder)
{
    using namespace efmi::frontend;
    const auto root = std::filesystem::absolute(folder / "host-values");
    std::filesystem::create_directories(root);
    const auto ini = root / "host.ini";
    write(ini, R"ini(namespace = host_case
[Constants]
global $fps = 0
global $dpi = 0
global $hotspot_x = 0
global $hotspot_y = 0
global $draw_dpi = 0
global $constant_dpi = 0
$constant_dpi = effective_dpi
[Present]
$fps = fps
$dpi = effective_dpi
$hotspot_x = cursor_hotspot_x
$hotspot_y = cursor_hotspot_y
[TextureOverrideHost]
hash = 12345678
$draw_dpi = effective_dpi
)ini");
    std::filesystem::remove(root / "settings" / "host.ini");
    RuntimeConfig config{root, {ini}, root / "settings", "host"};
    std::string reason;
    auto runtime = IniRuntime::load(config, {}, reason);
    if (!runtime) throw std::runtime_error(reason);
    check(runtime->status().diagnostics == 0, "Host value fixture parses without original warnings");
    auto value = [&](const char* name) { return runtime->status().variables.at(std::string("$\\host_case\\") + name); };
    auto present = [&](const RuntimeInput& input) { if (!runtime->present(input, 1, 0.f, reason)) throw std::runtime_error(reason); };
    check(value("constant_dpi") == 96, "Constants read the original's 96 before any sample");

    // FPSCounter: the first tick only initializes, then an exponential average of frame times
    // with smoothing 0.1, ignoring pauses over one second (min_fps 1).
    RuntimeInput input; input.foreground = true;
    present(input);
    check(value("fps") == 0 && value("dpi") == 96 && value("hotspot_x") == 0, "Without host samples fps is 0, effective_dpi 96 and the hotspot the origin");
    input.ticks_us = 5000000; present(input);
    check(value("fps") == 0, "The first host tick only starts the FPS counter");
    input.ticks_us += 16000; present(input);
    float average = 16000 / 1000000.0f;
    check(value("fps") == static_cast<float>(1.0f / average), "One frame time gives its rate");
    input.ticks_us += 20000; present(input);
    average += (20000 / 1000000.0f - average) * 0.1f;
    const float smoothed = static_cast<float>(1.0f / average);
    check(value("fps") == smoothed, "Later frame times enter the original average");
    input.ticks_us += 2000000; present(input);
    check(value("fps") == smoothed, "A pause over one second is ignored");
    input.ticks_us = 0; present(input);
    check(value("fps") == smoothed, "A presentation without a host tick leaves the counter");

    input.effective_dpi = 144;
    input.cursor = {true, 10, 20, 7, 9};
    present(input);
    check(value("dpi") == 144 && value("hotspot_x") == 7 && value("hotspot_y") == 9, "effective_dpi and the hotspot are the host's sample");
    RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30; draw.frame = 1;
    efmi::assets::FilePlan plan;
    check(runtime->draw(draw, plan, reason) && value("draw_dpi") == 144, "Draws read the latest effective DPI");
    input.effective_dpi = 72; present(input);
    check(value("dpi") == 96, "The original reports at least 96");
    input.effective_dpi = 70000;
    check(!runtime->present(input, 1, 0.f, reason), "An implausible DPI sample is refused");
}
