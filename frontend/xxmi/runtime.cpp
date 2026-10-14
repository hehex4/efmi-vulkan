#include "runtime_host.h"
#include "resource_plan.h"
#include "globals.h"
#include "HackerContext.h"
#include "Override.h"
#include "ResourceHash.h"
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <mutex>
#include <cmath>
#include <unordered_set>

void HostParseExcerpt(const wchar_t*, const wchar_t*);
void HostParseIncludes();
void HostCompileRemainingLists(const std::vector<std::wstring>&);
void HostCompileEntries(const std::vector<ExplicitCommandListSection*>&, const std::vector<std::wstring>&);
const std::vector<ExplicitCommandListSection*>& RuntimeInputDependencies();
void RuntimeResetInputDependencies();
void HostParseConstants();
void HostEnumerateLists();
void HostParseResources();
void HostParseTextureOverrides();
void HostCompileRoots(const std::vector<CommandList*>&, const std::vector<std::wstring>&);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
std::string HostDiagnosticsSince(size_t);
void RuntimeResetParser();
void RuntimeParseKeys();
void RuntimeEnumerateCustomShaders();
void RuntimeCompilePresent();
void RuntimeCompileConstants(CommandList&);
void RuntimeValidateSections(const std::vector<std::wstring>&);
void RuntimeSwapInput(std::vector<InputAction*>&);
size_t RuntimeInputCount();
bool RuntimeDispatchInput(HackerDevice*, const efmi::frontend::RuntimeInput&);
extern std::vector<CommandList*> pending_post_command_lists;
extern std::vector<std::shared_ptr<CommandList>> dynamically_allocated_command_lists;

// Only this library defines these in production. The standalone old host still
// has its own executable globals and does not link the runtime translation unit.
Globals* G = nullptr;
FILE* LogFile = nullptr;
bool gLogDebug = false;
LogVerbosity gLogVerbosity = LogVerbosity::INVALID;

namespace efmi::frontend {
namespace {
std::mutex engine_mutex;
DrawPlanAdapter* input_adapter = nullptr;
HackerContext* input_context = nullptr;

std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid UTF-16 text");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
void read_ini(const std::filesystem::path& path, const std::wstring& name)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || in.tellg() > (32 << 20)) throw std::runtime_error("Cannot read INI or input exceeds budget: " + path.u8string());
    std::string bytes(size_t(in.tellg()), '\0'); in.seekg(0); in.read(bytes.data(), bytes.size());
    if (!in) throw std::runtime_error("INI read failed");
    std::wstring text;
    if (!bytes.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), nullptr, 0);
        if (!n) throw std::runtime_error("INI is not valid UTF-8");
        text.resize(n); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), text.data(), n);
        if (!text.empty() && text.front() == 0xfeff) text.erase(text.begin());
        for (size_t at = 0; (at = text.find(L"\r\n", at)) != std::wstring::npos;) text.erase(at, 1);
    }
    HostParseExcerpt(text.c_str(), name.empty() ? nullptr : name.c_str());
}
struct EngineState {
    std::unique_ptr<Globals> globals;
    CustomResources resources;
    CustomResourcePools pools;
    CustomShaders shaders;
    ExplicitCommandListSections lists;
    CommandListVariables variables;
    std::vector<CommandListVariable*> persistent;
    std::map<std::wstring, float> unknown;
    std::vector<CommandList*> registered, post;
    std::vector<std::shared_ptr<CommandList>> allocated;
    PresetOverrideMap presets;
    OverrideTransition transition;
    OverrideGlobalSave saved;
    std::vector<InputAction*> input;
    EngineState() : globals(std::make_unique<Globals>())
    {
        InitializeCriticalSection(&globals->mCriticalSection);
        InitializeCriticalSection(&globals->mShaderBindingsLock);
        InitializeCriticalSection(&globals->mResourcesLock);
        static std::once_flag creation_lock;
        std::call_once(creation_lock, [] { InitializeCriticalSection(&resource_creation_mode_lock); });
    }
    void swap()
    {
        customResources.swap(resources); customResourcePools.swap(pools); customShaders.swap(shaders);
        explicitCommandListSections.swap(lists); command_list_globals.swap(variables);
        persistent_variables.swap(persistent); unknown_variables.swap(unknown);
        registered_command_lists.swap(registered); pending_post_command_lists.swap(post);
        dynamically_allocated_command_lists.swap(allocated); presetOverrides.swap(presets);
        CurrentTransition.params.swap(transition.params); CurrentTransition.vars.swap(transition.vars);
        OverrideSave.params.swap(saved.params); OverrideSave.vars.swap(saved.vars);
        RuntimeSwapInput(input);
    }
    ~EngineState()
    {
        for (auto* action : input) delete action;
        DeleteCriticalSection(&globals->mCriticalSection);
        DeleteCriticalSection(&globals->mShaderBindingsLock);
        DeleteCriticalSection(&globals->mResourcesLock);
    }
};
struct Scope {
    std::unique_lock<std::mutex> lock;
    EngineState& state;
    Scope(EngineState& s) : lock(engine_mutex), state(s)
    {
        if (G) throw std::runtime_error("Nested frontend generation activation");
        G = state.globals.get(); state.swap();
    }
    ~Scope()
    {
        input_adapter = nullptr; input_context = nullptr;
        clear_command_list_profiling(); state.swap(); G = nullptr;
    }
};
void f10(HackerDevice*, void*) { G->gReloadConfigPending = true; }
}

struct IniRuntime::Impl {
    EngineState state;
    RuntimeConfig config;
    std::filesystem::path settings;
    std::shared_ptr<RuntimeExtension> extension;
    DrawPlanAdapter adapter;
    std::unique_ptr<FilePlanCompiler> files;
    HackerDevice device;
    HackerContext context;
    RuntimeStatus stats;
    RuntimeInput last_input;
    std::array<bool, 256> blocked_keys{};
    bool can_save = true;
    std::vector<std::shared_ptr<const assets::FileResource>> resources;
    Impl() { device.mHackerContext = &context; context.mHackerDevice = &device; }
    CommandListState command_state()
    {
        CommandListState s; s.mHackerContext = &context; s.mHackerDevice = &device; return s;
    }
    bool execute(CommandList& pre, CommandList& post, assets::FilePlan& result, std::wstring& reason)
    {
        auto s = command_state(); DrawPlan plan;
        return adapter.execute_pair(pre, post, s, plan, reason) && files->compile(plan, result, reason);
    }
};

IniRuntime::IniRuntime() : impl_(std::make_unique<Impl>()) {}
IniRuntime::~IniRuntime()
{
    // Original command/resource destructors share frontend infrastructure too.
    // Do not let teardown race a candidate parsing or the active draw evaluator.
    std::lock_guard<std::mutex> lock(engine_mutex);
    impl_.reset();
}

std::shared_ptr<IniRuntime> IniRuntime::load(const RuntimeConfig& config, std::shared_ptr<RuntimeExtension> extension, std::string& reason)
{
    const char* phase = "input loading";
    try {
        auto runtime = std::shared_ptr<IniRuntime>(new IniRuntime);
        auto& p = *runtime->impl_; p.config = config; p.extension = std::move(extension);
        if ((config.inputs.empty() && config.mods_directory.empty()) || config.inputs.size() > 128 || !config.resource_root.is_absolute() ||
            !config.settings_directory.is_absolute() || config.settings_name.empty() || config.settings_name.size() > 80 ||
            config.settings_name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
            throw std::runtime_error("Invalid frontend input/settings location");
        p.settings = config.settings_directory / (config.settings_name + ".ini");
        Scope scope(p.state); RuntimeResetParser(); RuntimeResetInputDependencies();
        const auto diagnostics = HostDiagnosticCount();
        HostResourceRoot(config.resource_root.wstring());
        if (!config.mods_directory.empty()) {
            auto relative = config.mods_directory.lexically_relative(config.resource_root);
            if (!config.mods_directory.is_absolute() || relative.empty() || *relative.begin() == L".." ||
                !std::filesystem::is_directory(config.mods_directory))
                throw std::runtime_error("Enabled Mods directory must be inside the resource root");
            const auto root = L"[Include]\ninclude_recursive = " + relative.wstring() +
                L"\nexclude_recursive = DISABLED*\nexclude_recursive = desktop.ini\n";
            HostParseExcerpt(root.c_str(), nullptr);
        }
        for (const auto& path : config.inputs) {
            if (!path.is_absolute()) throw std::runtime_error("INI input path must be absolute");
            auto relative = path.lexically_relative(config.resource_root);
            read_ini(path, relative.empty() || *relative.begin() == L".." ? path.filename().wstring() : relative.wstring());
        }
        HostParseIncludes();
        // The original loader may declare user_config, but our persistence
        // destination is always the explicit project-owned generation path.
        G->user_config = p.settings.wstring();
        RuntimeValidateSections(p.extension ? p.extension->native_shaders() : std::vector<std::wstring>{});
        if (p.extension) {
            const auto declarations = p.extension->declarations();
            if (!declarations.empty()) HostParseExcerpt(declarations.c_str(), L"efvk-native.ini");
            size_t unit = 0;
            for (const auto& text : p.extension->declaration_units()) {
                const auto name = L"efvk-library-" + std::to_wstring(unit++) + L".ini";
                HostParseExcerpt(text.c_str(), name.c_str());
            }
        }
        // Original [Constants] parsing consumes declarations and later executes
        // assignments from the generated settings file after default values.
        if (std::filesystem::exists(p.settings)) read_ini(p.settings, L"efvk-user-settings.ini");
        phase = "declaration parsing";
        RuntimeEnumerateCustomShaders(); HostEnumerateLists(); HostParseConstants(); HostParseResources();
        auto boundaries = p.extension ? p.extension->install(p.adapter) : std::vector<std::wstring>{};
        HostParseTextureOverrides(); RuntimeCompilePresent(); RuntimeParseKeys();
        std::wstring validation;
        if (p.extension && !p.extension->validate_overrides(validation)) throw std::runtime_error("Native override registry refused: " + utf8(validation));
        RegisterKeyBinding(L"reload_config", L"no_modifiers VK_F10", std::make_shared<InputCallbacks>(f10, nullptr, nullptr), 0, 0, 0, InputDisableScope::ALL);
        CommandList constants; RuntimeCompileConstants(constants);
        auto roots = registered_command_lists;
        phase = "command parsing";
        HostCompileRoots(roots, boundaries);
        HostCompileEntries(RuntimeInputDependencies(), boundaries);
        RuntimeResetInputDependencies();
        // Directory generations validate all enabled files. Legacy explicit
        // profiles intentionally import only a reachable Core subgraph; those
        // partial inputs do not declare the rest of the Core's dependencies.
        if (!config.mods_directory.empty()) HostCompileRemainingLists(boundaries);
        auto all_roots = registered_command_lists;
        // The optimizer releases its temporary ownership of branch lists.
        // Removed branches then die; only adapt surviving children through
        // their owning IfCommand, never through the old raw registration list.
        std::unordered_set<CommandList*> branch_lists;
        for (const auto& branch : dynamically_allocated_command_lists) branch_lists.insert(branch.get());
        all_roots.erase(std::remove_if(all_roots.begin(), all_roots.end(),
            [&](const auto* root) { return branch_lists.count(const_cast<CommandList*>(root)); }), all_roots.end());
        if (G->iniParamsReserved < 0 || G->iniParamsReserved > 65536)
            throw std::runtime_error("INI parameter reservation exceeds generation budget");
        G->iniParams.assign(G->iniParamsReserved, DirectX::XMFLOAT4(0, 0, 0, 0));
        phase = "original command optimization";
        optimise_command_lists(nullptr);
        // Includes key-owned activation/post lists as well as Present and the
        // original hash matcher lists; do not adapt only named callbacks.
        phase = "Vulkan command adaptation";
        for (auto* root : all_roots) p.adapter.adapt(*root);
        p.adapter.adapt(constants);
        p.adapter.adapt(G->present_command_list); p.adapter.adapt(G->post_present_command_list);
        for (auto& hashed : G->mTextureOverrideMap) for (auto& entry : hashed.second) {
            p.adapter.adapt(entry.command_list); p.adapter.adapt(entry.post_command_list);
        }
        phase = "file declaration adaptation";
        p.files = std::make_unique<FilePlanCompiler>(customResources);
        auto initial = p.command_state(); DrawPlan unused; std::wstring why;
        if (!p.adapter.execute(constants, initial, unused, why) || !unused.items.empty())
            throw std::runtime_error("Constants execution refused: " + utf8(why));
        if (p.extension && !p.extension->prepare(p.adapter, initial, why))
            throw std::runtime_error("Native generation preparation refused: " + utf8(why));
        if (HostDiagnosticCount() != diagnostics) throw std::runtime_error("Original INI parser: " + HostDiagnosticsSince(diagnostics));
        // Collect every branch's reachable files, not dormant declarations.
        // Original resource copy/alias commands are included by the adapter.
        for (auto* resource : p.adapter.referenced_files()) {
            for (auto operation : {PlanOperation::BindVertex, PlanOperation::BindIndex, PlanOperation::BindPixelResource}) {
                DrawPlan plan; PlanItem item; item.operation = operation; item.resource = resource; plan.items.push_back(item);
                assets::FilePlan owned;
                if (p.files->compile(plan, owned, why) && owned.items[0].resource) p.resources.push_back(owned.items[0].resource);
            }
        }
        p.stats.bindings = RuntimeInputCount(); G->user_config_dirty = false;
        RuntimeResetParser(); return runtime;
    } catch (const std::exception& e) { reason = std::string(phase) + ": " + e.what(); return {}; }
}

bool IniRuntime::draw(const RuntimeDraw& input, assets::FilePlan& output, std::string& reason)
{
    try {
        auto& p = *impl_; Scope scope(p.state);
        G->frame_no = unsigned(input.frame); G->gTime = input.seconds;
        DrawCallInfo call(DrawCall::DrawIndexedInstanced, 0, input.count, input.instances, uint32_t(input.base), input.first, input.first_instance, nullptr, 0);
        TextureOverrideMatches matches; find_texture_override_for_hash(input.hash, &matches, &call);
        assets::FilePlan result;
        auto s = p.command_state(); s.call_info = &call; std::wstring why;
        ResourceCopyTarget index_target;
        index_target.type = ResourceCopyTargetType::INDEX_BUFFER;
        s.this_target = &index_target;
        if (p.extension && !p.extension->before_draw(input, s, why)) throw std::runtime_error(utf8(why));
        for (auto* match : matches) {
            DrawPlan plan; assets::FilePlan owned;
            if (!p.adapter.execute_pair(match->command_list, match->post_command_list, s, plan, why) || !p.files->compile(plan, owned, why))
                throw std::runtime_error("Original draw command refused: " + utf8(why));
            result.skip_original |= owned.skip_original;
            result.items.insert(result.items.end(), owned.items.begin(), owned.items.end());
        }
        ++p.stats.draws; output = std::move(result); return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}

void RuntimeRunInputList(CommandList* list, bool post)
{
    if (!input_adapter || !input_context) throw std::runtime_error("Input command outside active generation");
    input_adapter->adapt(*list);
    CommandListState state; state.mHackerContext = input_context; state.mHackerDevice = input_context->mHackerDevice; state.post = post;
    DrawPlan result; std::wstring why;
    if (!input_adapter->execute(*list, state, result, why) || !result.items.empty())
        throw std::runtime_error("Input command requires unavailable render context: " + utf8(why));
}
bool IniRuntime::present(const RuntimeInput& input, uint64_t frame, float seconds, std::string& reason)
{
    try {
        auto& p = *impl_; Scope scope(p.state); G->frame_no = unsigned(frame); G->gTime = seconds;
        p.last_input = input;
        auto sampled = input;
        for (size_t i = 0; i < sampled.keys.size(); ++i) {
            if (!input.foreground || !input.keys[i]) p.blocked_keys[i] = false;
            if (p.blocked_keys[i]) sampled.keys[i] = false;
        }
        input_adapter = &p.adapter; input_context = &p.context;
        if (RuntimeDispatchInput(&p.device, sampled)) ++p.stats.key_events;
        CurrentTransition.UpdatePresets(&p.device); CurrentTransition.UpdateTransitions(&p.device);
        assets::FilePlan ignored; std::wstring why;
        if (!p.execute(G->present_command_list, G->post_present_command_list, ignored, why) || !ignored.items.empty())
            throw std::runtime_error("Present requires unavailable render context: " + utf8(why));
        ++p.stats.presents; return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}

bool IniRuntime::save(std::string& reason)
{
    try {
        auto& p = *impl_; Scope scope(p.state);
        if (!p.can_save || !G->user_config_dirty) return true;
        std::ostringstream text; text.imbue(std::locale::classic());
        text << "; EFMI Vulkan persistent original INI variables\n[Constants]\n" << std::setprecision(9);
        for (const auto* value : persistent_variables) {
            if (!std::isfinite(value->fval)) throw std::runtime_error("Non-finite persistent value");
            // Keep project text ASCII even if an original namespace is not.
            auto name = utf8(value->name);
            if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c >= 128; }))
                throw std::runtime_error("Persistent variable name is not ASCII");
            text << name << " = " << value->fval << '\n';
        }
        std::filesystem::create_directories(p.config.settings_directory);
        const auto temp = p.settings.wstring() + L".tmp";
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create settings temporary file");
        const auto bytes = text.str(); DWORD written = 0;
        const bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok || !MoveFileExW(temp.c_str(), p.settings.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot atomically publish settings; previous file retained");
        G->user_config_dirty = false; ++p.stats.saves; return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
RuntimeStatus IniRuntime::status() const
{
    auto& p = *impl_; Scope scope(p.state); auto result = p.stats;
    result.dirty = G->user_config_dirty; result.reload_requested = G->gReloadConfigPending;
    for (const auto& item : command_list_globals) result.variables[utf8(item.first)] = item.second.fval;
    return result;
}
std::vector<std::shared_ptr<const assets::FileResource>> IniRuntime::resources() const
{
    return impl_->resources;
}
bool IniRuntime::take_reload_request()
{
    Scope scope(impl_->state); const bool result = G->gReloadConfigPending; G->gReloadConfigPending = false; return result;
}
void IniRuntime::suspend_persistence()
{
    Scope scope(impl_->state); impl_->can_save = false;
}
void IniRuntime::inherit_persistent(const IniRuntime& previous)
{
    std::map<std::wstring, float> values;
    RuntimeInput held;
    { Scope scope(previous.impl_->state); held = previous.impl_->last_input; for (const auto* value : persistent_variables) values.emplace(value->name, value->fval); }
    { Scope scope(impl_->state);
      if (held.foreground) impl_->blocked_keys = held.keys;
      for (auto* value : persistent_variables) {
        const auto found = values.find(value->name);
        if (found != values.end() && value->fval != found->second) { value->fval = found->second; G->user_config_dirty = true; }
    } }
}
}
