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
