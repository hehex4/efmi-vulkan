// Original LoadConfigFile/ParseIncludedIniFiles behavior without Mod discovery.
static void runtime_configuration_cases(const std::filesystem::path& fixtures)
{
    const auto root = std::filesystem::absolute(fixtures / "configuration");
    std::filesystem::create_directories(root / "Mods" / "A");
    std::filesystem::create_directories(root / "Mods" / "B");
    std::filesystem::create_directories(root / "Mods" / "DISABLED-unused");
    std::filesystem::create_directories(root / "Libraries");
    write(root / "d3dx.ini", R"ini([Constants]
global $root_value = 17
[Include]
include = Libraries/helper.ini
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
)ini");
    write(root / "Libraries" / "helper.ini", R"ini(namespace = helper
[Constants]
global $calls = 0
[CommandListTouch]
$calls = $calls + 1
)ini");
    write(root / "Mods" / "A" / "first.ini", R"ini(namespace = shared
[Constants]
global $alpha = 0
global $initializer_calls = 0
[ResourceNeverUsed]
type = Buffer
stride = 12
filename = deliberately-missing.buf
[CommandListInitializeMergedSkeleton]
$initializer_calls = $initializer_calls + 1
[TextureOverrideFirst]
hash = 12345678
match_index_count = 30
$alpha = 7
run = CommandList\helper\Touch
run = CommandListSecond
handling = skip
drawindexed = 3,0,0
)ini");
    write(root / "Mods" / "B" / "second.ini", R"ini(namespace = shared
[Constants]
global $beta = 0
[CommandListSecond]
$beta = $beta + 5
)ini");
    write(root / "Mods" / "DISABLED-unused" / "hidden.ini", R"ini(namespace = hidden
[Constants]
global $unexpected = 1
)ini");
    RuntimeConfig config{root, {}, root / "settings", "configuration"};
    config.installation = root;
    std::string reason;
    auto runtime = IniRuntime::load(config, {}, reason);
    if (!runtime) throw std::runtime_error("Original configuration load: " + reason);
    check(runtime->status().variables.at("$root_value") == 17, "Root configuration constants execute");
    check(runtime->status().variables.at("$\\shared\\initializer_calls") == 0,
          "A named initializer is not executed by loader discovery");
    check(runtime->status().variables.count("$\\hidden\\unexpected") == 0,
          "Original recursive exclusions apply to the complete configuration");
    check(!std::filesystem::exists(root / "Mods" / "A" / "deliberately-missing.buf"),
          "Unused missing resources do not gate configuration loading");
    RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30;
    efmi::assets::FilePlan plan;
    check(runtime->draw(draw, plan, reason), "Global hash rules execute without a package");
    check(plan.skip_original && plan.items.size() == 1 && plan.items[0].index_count == 3,
          "Original command lists supply the draw plan");
    const auto state = runtime->status();
    check(state.variables.at("$\\shared\\alpha") == 7 && state.variables.at("$\\shared\\beta") == 5,
          "Two included files sharing a namespace both contribute their distinct sections");
    check(state.variables.at("$\\helper\\calls") == 1,
          "Cross-directory command references execute in the same registry");
    auto second = IniRuntime::load(config, {}, reason);
    check(bool(second), "A complete successor configuration loads independently");
    check(second->status().variables.at("$\\shared\\beta") == 0 && runtime->status().variables.at("$\\shared\\beta") == 5,
          "Old recorded generation state survives construction of its successor");
}
