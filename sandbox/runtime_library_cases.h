// A library the Mod calls by namespace from outside its folder (D-112): parsed after the Mod's INI
// under its path relative to the Mod's folder, so the files it names resolve against its own folder,
// and a declaration it holds answered in a draw's plan as the original answers it.
static void runtime_library_cases(const std::filesystem::path& folder)
{
    using namespace efmi::frontend;
    const auto mods = std::filesystem::absolute(folder / "library-case" / "Mods");
    const auto mod = mods / "mod", library = mods / "lib";
    std::filesystem::create_directories(mod);
    std::filesystem::create_directories(library / "data");
    write(library / "data" / "own.buf", "\x01\x00\x02\x00");
    write(mod / "texture.buf", "\x03\x00\x04\x00");
    write(library / "lib.ini", R"ini(namespace = lib_case
[Constants]
global $masked = -1
global $own = -1
global $cleared = -1
global $count = 0
[ResourceMask]
[ResourceOwn]
type = Buffer
format = R16_UINT
filename = data\own.buf
[CommandListProbe]
if ResourceMask !== null
    $masked = 1
else
    $masked = 0
endif
if ResourceOwn !== null
    $own = 1
endif
ResourceMask = null
if ResourceMask === null
    $cleared = 1
endif
$count = 0
)ini");
    write(mod / "mod.ini", R"ini(namespace = mod_case
[ResourceTexture]
type = Buffer
format = R16_UINT
filename = texture.buf
[TextureOverrideDraw]
hash = 12345678
Resource\lib_case\Mask = ref ResourceTexture
$\lib_case\count = 30
run = CommandList\lib_case\Probe
[TextureOverrideBare]
hash = 87654321
ib = Resource\lib_case\Own
$\lib_case\count = 30
run = CommandList\lib_case\Probe
)ini");
    std::filesystem::remove(folder / "library-case" / "settings" / "library.ini");
    RuntimeConfig config{mod, {mod / "mod.ini"}, folder / "library-case" / "settings", "library"};
    config.libraries = {library / "lib.ini"};
    std::string reason;
    auto runtime = IniRuntime::load(config, {}, reason);
    if (!runtime) throw std::runtime_error(reason);
    check(runtime->status().diagnostics == 0, "A library called by namespace parses with the Mod without original warnings");
    bool own_file = false;
    std::string seen;
    for (const auto& file : runtime->resources()) {
        std::error_code error;
        own_file |= std::filesystem::equivalent(file->path, library / "data" / "own.buf", error);
        seen += " " + file->path.u8string();
    }
    if (!own_file) throw std::runtime_error("A library's file resolves against the library's own folder, not the Mod's:" + seen);
    auto value = [&](const char* name) { return runtime->status().variables.at(std::string("$\\lib_case\\") + name); };
    RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30; draw.frame = 1;
    efmi::assets::FilePlan plan;
    if (!runtime->draw(draw, plan, reason)) throw std::runtime_error("A draw calling the library's list plans: " + reason);
    check(value("masked") == 1 && value("own") == 1 && value("cleared") == 1 && value("count") == 0,
          "In a draw's plan a declaration holding a resource is not null, one set to null is (the original's -0)");
    draw.hash = 0x87654321; draw.frame = 2;
    if (!runtime->draw(draw, plan, reason)) throw std::runtime_error("A draw without the assignment plans: " + reason);
    check(value("masked") == 0, "A declaration never assigned is null in the plan");
}
