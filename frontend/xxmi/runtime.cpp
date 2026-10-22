#include "runtime_host.h"
#include "resource_plan.h"
#include "shader_regex.h"
#include "globals.h"
#include "HackerContext.h"
#include "Override.h"
#include "ResourceHash.h"
#include <chrono>
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
void HostParseResourceMatching();
void HostParseShaderOverrides();
void HostParseShaderRegex();
void HostCompileRoots(const std::vector<CommandList*>&, const std::vector<std::wstring>&);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
std::string HostDiagnosticsSince(size_t);
std::vector<std::string> HostDiagnosticListSince(size_t);
void RuntimeResetParser();
void RuntimeParseKeys();
void RuntimeEnumerateCustomShaders();
void RuntimeCompilePresent();
void RuntimeCompileConstants(CommandList&,CommandList&);
void RuntimeParseParameterRegister();
void RuntimeUpdateTransitions(HackerDevice*);
void RuntimeValidateSections(const std::vector<std::wstring>&,bool custom_shaders,bool shader_hooks);
void RuntimeSwapInput(std::vector<InputAction*>&);
size_t RuntimeInputCount();
bool RuntimeDispatchInput(HackerDevice*, const efmi::frontend::RuntimeInput&);
void RuntimeWindowSample(const efmi::frontend::RuntimeInput*);
HWND RuntimeWindowHandle();
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
    ShaderRegexGroups regex;
    std::vector<ShaderRegexGroup*> regex_index;
    uint32_t regex_hash=0;
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
    // The latest host window and cursor sample; the original's window queries read it.
    efmi::frontend::RuntimeInput sample;
    EngineState() : globals(std::make_unique<Globals>())
    {
        // IniHandler's values when [System] screen_width and screen_height are absent; a
        // generation reads no d3dx.ini. window_width falls back to them last.
        globals->gFallbackScreenWidth = 1920; globals->gFallbackScreenHeight = 1080;
        InitializeCriticalSection(&globals->mCriticalSection);
        InitializeCriticalSection(&globals->mShaderBindingsLock);
        InitializeCriticalSection(&globals->mResourcesLock);
        static std::once_flag creation_lock;
        std::call_once(creation_lock, [] { InitializeCriticalSection(&resource_creation_mode_lock); });
    }
    void swap()
    {
        customResources.swap(resources); customResourcePools.swap(pools); customShaders.swap(shaders);
        shader_regex_groups.swap(regex);shader_regex_group_index.swap(regex_index);std::swap(shader_regex_hash,regex_hash);
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
        G = state.globals.get(); state.swap(); RuntimeWindowSample(&state.sample);
    }
    ~Scope()
    {
        input_adapter = nullptr; input_context = nullptr; RuntimeWindowSample(nullptr);
        clear_command_list_profiling(); state.swap(); G = nullptr;
    }
};
void f10(HackerDevice*, void*) { G->gReloadConfigPending = true; }
}

struct IniRuntime::Impl {
    const RuntimeDraw* current_draw = nullptr; // during draw(): the host's texture identities
    EngineState state;
    RuntimeConfig config;
    std::filesystem::path settings;
    std::shared_ptr<RuntimeExtension> extension;
    DrawPlanAdapter adapter;
    SourceShaderContext source_shaders;
    SourceShaderRegex source_regex;
    std::unique_ptr<FilePlanCompiler> files;
    HackerDevice device;
    HackerContext context;
    RuntimeStatus stats;
    RuntimeInput last_input;
    std::array<bool, 256> blocked_keys{};
    bool can_save = true;
    // The variable and parameter state the last present() started from, for replay() (BUG-036).
    std::vector<std::pair<CommandListVariable*, float>> pre_variables;
    std::vector<DirectX::XMFLOAT4> pre_parameters;
    bool replayable = false;
    std::vector<std::shared_ptr<const assets::FileResource>> resources;
    Impl() { device.mHackerContext = &context; context.mHackerDevice = &device; }
    CommandListState command_state()
    {
        CommandListState s; s.mHackerContext = &context; s.mHackerDevice = &device; return s;
    }
    bool execute(CommandList& pre, CommandList& post, assets::FilePlan& result, std::wstring& reason,DrawPlanAdapter::OriginalCall between={})
    {
        auto s = command_state(); DrawPlan plan;
        return adapter.execute_pair(pre, post, s, plan, reason,std::move(between)) && files->compile(plan, result, reason);
    }
};

IniRuntime::IniRuntime() : impl_(std::make_unique<Impl>()) {}
IniRuntime::~IniRuntime()
{
    // Original command/resource destructors share frontend infrastructure too.
    // Do not let teardown race a candidate parsing or the active draw evaluator.
    std::lock_guard<std::mutex> lock(engine_mutex);
    // Original destructors may write through G (~UpscalingFlipBBCommand clears a flag), so
    // they address this generation's globals, which outlive every other member of the state.
    G = impl_->state.globals.get();
    impl_.reset();
    G = nullptr;
}

std::shared_ptr<IniRuntime> IniRuntime::load(const RuntimeConfig& config, std::shared_ptr<RuntimeExtension> extension, std::string& reason)
{
    const char* phase = "input loading";
    try {
        auto runtime = std::shared_ptr<IniRuntime>(new IniRuntime);
        auto& p = *runtime->impl_; p.config = config; p.extension = std::move(extension);
        struct LoadGuard {
            std::shared_ptr<RuntimeExtension> extension;bool complete=false;
            ~LoadGuard(){if(!complete && extension)extension->execution_failed();}
        } load_guard{p.extension};
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
        RuntimeValidateSections(p.extension ? p.extension->native_shaders() : std::vector<std::wstring>{},
                                p.extension && p.extension->custom_shader_execution(),config.shader_hooks);
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
        RuntimeEnumerateCustomShaders(); HostEnumerateLists(); HostParseConstants(); HostParseResources();RuntimeParseParameterRegister();
        if(config.shader_hooks){
            if(config.initial_shaders)p.source_shaders.install(*config.initial_shaders,p.context);
            p.adapter.shader_queries([&p](auto& operand,auto& state,auto& value,auto& why){return p.source_shaders.query(operand,state,value,why);});
        }else if(config.initial_shaders)throw std::runtime_error("Source shader facts require shader hook opt-in");
        p.adapter.host_values([&p](ParamOverrideType type,float& value){
            const auto& sample=p.state.sample;
            if(type==ParamOverrideType::EFFECTIVE_DPI){value=(std::max)(float(sample.effective_dpi),96.0f);return true;}
            if(type==ParamOverrideType::CURSOR_HOTSPOT_X){value=float(sample.cursor.hotspot_x);return true;}
            if(type==ParamOverrideType::CURSOR_HOTSPOT_Y){value=float(sample.cursor.hotspot_y);return true;}
            return false;
        });
        if (config.texture_identity && !config.shader_hooks) {
            p.adapter.texture_queries([&p](unsigned slot, const DrawPlanAdapter::PixelBinding& binding, CommandListState&,
                                           DrawPlanAdapter::TextureIdentity& id, std::wstring& why) {
                // A resource of the generation: the original keeps no hash for what it creates itself.
                if (!binding.application) { id.bound = binding.resource != nullptr; id.hash = 0; return true; }
                if (!p.current_draw || !p.current_draw->pixel_texture) { why = L"Source texture identity unavailable outside a draw"; return false; }
                bool bound = false; uint32_t hash = 0; std::string reason;
                if (!p.current_draw->pixel_texture(slot, bound, hash, reason)) { why.assign(reason.begin(), reason.end()); return false; }
                id.bound = bound; id.hash = hash; return true;
            });
        }
        auto boundaries = p.extension ? p.extension->install(p.adapter) : std::vector<std::wstring>{};
        if(config.shader_hooks){HostParseShaderOverrides();HostParseShaderRegex();
            for(const auto& entry:G->mShaderOverrideMap)
                if(entry.second.depth_filter!=DepthBufferFilter::NONE || entry.second.model[0])
                    throw std::runtime_error("Legacy shader depth/model substitution requires a source replacement adapter");
        }
        HostParseTextureOverrides();HostParseResourceMatching(); RuntimeCompilePresent(); RuntimeParseKeys();
        std::wstring validation;
        if (p.extension && !p.extension->validate_overrides(validation)) throw std::runtime_error("Native override registry refused: " + utf8(validation));
        RegisterKeyBinding(L"reload_config", L"no_modifiers VK_F10", std::make_shared<InputCallbacks>(f10, nullptr, nullptr), 0, 0, 0, InputDisableScope::ALL);
        CommandList constants,post_constants; RuntimeCompileConstants(constants,post_constants);
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
        if(config.shader_hooks)p.source_regex.initialize(p.adapter);
        // Includes key-owned activation/post lists as well as Present and the
        // original hash matcher lists; do not adapt only named callbacks.
        phase = "Vulkan command adaptation";
        for (auto* root : all_roots) p.adapter.adapt(*root);
        p.adapter.adapt(constants);p.adapter.adapt(post_constants);
        p.adapter.adapt(G->present_command_list); p.adapter.adapt(G->post_present_command_list);
        for (auto& hashed : G->mTextureOverrideMap) for (auto& entry : hashed.second) {
            p.adapter.adapt(entry.command_list); p.adapter.adapt(entry.post_command_list);
        }
        for(auto& entry:G->mShaderOverrideMap){p.adapter.adapt(entry.second.command_list);p.adapter.adapt(entry.second.post_command_list);}
        if(config.shader_hooks && config.initial_shaders)p.source_regex.apply(p.source_regex.prepare(*config.initial_shaders,63,false),p.adapter);
        phase = "file declaration adaptation";
        p.files = std::make_unique<FilePlanCompiler>(customResources);
        auto initial = p.command_state(); DrawPlan unused; std::wstring why;
        if(p.extension && !p.extension->initialize_parameters(initial,why))
            throw std::runtime_error("GPU parameter initialization refused: "+utf8(why));
        if (!p.adapter.execute(constants, initial, unused, why) || !unused.items.empty())
            throw std::runtime_error("Constants execution refused: " + utf8(why));
        // Match InitIniParams: defaults and persisted pre values are not dirty;
        // post-Constants assignments can make the user settings dirty again.
        G->user_config_dirty=false;
        auto post_initial=p.command_state();post_initial.post=true;
        if(!p.adapter.execute(post_constants,post_initial,unused,why) || !unused.items.empty())
            throw std::runtime_error("Post-Constants execution refused: "+utf8(why));
        if (p.extension && !p.extension->prepare(p.adapter, initial, why))
            throw std::runtime_error("Native generation preparation refused: " + utf8(why));
        // Original warnings (a duplicate section, an unrecognized line, a disabled regex group) do
        // not stop the original loader: it keeps what it could parse and reports the rest. The
        // generation does the same and keeps the texts for the status (D-067). What this host
        // cannot execute is refused by the adapters, independently of these warnings.
        p.stats.diagnostics = HostDiagnosticCount() - diagnostics;
        p.stats.diagnostic_messages = HostDiagnosticListSince(diagnostics);
        // Collect every branch's reachable files, not dormant declarations.
        // Original resource copy/alias commands are included by the adapter.
        for (auto* resource : p.adapter.referenced_files()) {
            for (auto operation : {PlanOperation::BindVertex, PlanOperation::BindIndex, PlanOperation::BindPixelResource}) {
                DrawPlan plan; PlanItem item; item.operation = operation; item.resource = resource; plan.items.push_back(item);
                assets::FilePlan owned;
                if (p.files->compile(plan, owned, why) && owned.items[0].resource) p.resources.push_back(owned.items[0].resource);
            }
        }
        p.stats.bindings = RuntimeInputCount();G->constants_run=true;
        RuntimeResetParser();load_guard.complete=true;return runtime;
    } catch (const std::exception& e) { reason = std::string(phase) + ": " + e.what(); return {}; }
}

void IniRuntime::note_timing(const char* name, uint64_t microseconds) { impl_->adapter.note_timing(name, microseconds); }

bool IniRuntime::draw(const RuntimeDraw& input, assets::FilePlan& output, std::string& reason)
{
    struct Completion {RuntimeExtension* extension;~Completion(){if(extension)extension->after_draw();}} completion{impl_->extension.get()};
    try {
        if(impl_->config.shader_hooks)throw std::runtime_error("Shader hook generation requires the intercepted call entry");
        if(!std::isfinite(input.frame_seconds) || input.frame_seconds<0)throw std::runtime_error("Original frame duration is invalid");
#if defined(EFMI_COMMAND_TIMING) || defined(EFMI_PHASE_TIMING)
        auto phase_began = std::chrono::steady_clock::now();
        auto phase = [&](const char* name) {
            const auto now = std::chrono::steady_clock::now();
            impl_->adapter.note_timing(name, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - phase_began).count()));
            phase_began = now;
        };
#else
        auto phase = [](const char*) {};
#endif
        auto& p = *impl_; Scope scope(p.state); phase("phase scope");
        struct Current {Impl& p; Current(Impl& owner, const RuntimeDraw& draw):p(owner){p.current_draw=&draw;} ~Current(){p.current_draw=nullptr;}} current(p, input);
        G->frame_no = unsigned(input.frame); G->gTime = input.seconds; G->gFrameTime = input.frame_seconds;
        // DRAW_NUMBER: the Core's identification tells a second identifier call within one draw by it.
        ++p.context.draw_number;
        DrawCallInfo call(DrawCall::DrawIndexedInstanced, 0, input.count, input.instances, uint32_t(input.base), input.first, input.first_instance, nullptr, 0);
        TextureOverrideMatches matches; find_texture_override_for_hash(input.hash, &matches, &call); phase("phase find");
        assets::FilePlan result;
        auto s = p.command_state(); s.call_info = &call; std::wstring why;
        ResourceCopyTarget index_target;
        index_target.type = ResourceCopyTargetType::INDEX_BUFFER;
        s.this_target = &index_target;
        if (p.extension && !p.extension->before_draw(input, s, why)) throw std::runtime_error(utf8(why));
        phase("phase before_draw");
        if(!matches.empty()){
            std::vector<CommandList*> pre,post;pre.reserve(matches.size());post.reserve(matches.size());
            for(auto* match:matches){pre.push_back(&match->command_list);post.push_back(&match->post_command_list);}
            DrawPlan plan;
            const auto began = std::chrono::steady_clock::now();
            const bool executed = p.adapter.execute_matches(pre,post,s,plan,why);
            const auto ran = std::chrono::steady_clock::now();
            const bool compiled = executed && p.files->compile(plan,result,why);
            p.stats.draw_execute_us += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(ran - began).count());
            p.stats.draw_compile_us += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - ran).count());
            if (!executed || !compiled) throw std::runtime_error("Original draw command refused: " + utf8(why));
            phase("phase execute+compile");
        }
        ++p.stats.draws; output = std::move(result); return true;
    } catch (const std::exception& e) {
        if(impl_->extension)impl_->extension->execution_failed();
        reason = e.what(); return false;
    }
}

bool IniRuntime::bind_source_shaders(const SourceShaderState& state,std::string& reason){
    reason.clear();try{auto& p=*impl_;Scope scope(p.state);
        if(!p.config.shader_hooks)throw std::runtime_error("Source shader facts require shader hook opt-in");
        auto prepared=p.source_regex.prepare(state,63,false);p.source_shaders.install(state,p.context);p.source_regex.apply(std::move(prepared),p.adapter);return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool IniRuntime::intercept(const RuntimeShaderCall& input,std::string& reason){
    reason.clear();try{auto& p=*impl_;Scope scope(p.state);
        const bool compute=input.call.type==DrawCall::Dispatch || input.call.type==DrawCall::DispatchIndirect;
        struct Completion {RuntimeExtension* extension;bool compute;~Completion(){if(extension){if(compute)extension->after_dispatch();else extension->after_draw();}}} completion{p.extension.get(),compute};
        if(!p.config.shader_hooks)throw std::runtime_error("Intercepted calls require shader hook opt-in");
        if(!std::isfinite(input.seconds) || !std::isfinite(input.frame_seconds) || input.frame_seconds<0)
            throw std::runtime_error("Original shader call time is invalid");
        if(input.call.type<DrawCall::Draw || input.call.type>DrawCall::DispatchIndirect || !input.original)
            throw std::runtime_error("Original shader call boundary is missing or invalid");
        const unsigned shader_mask=compute?32u:31u;
        auto prepared=input.call.hunting_skip?SourceShaderRegex::Preparation{}:p.source_regex.prepare(input.shaders,shader_mask,true);
        p.source_shaders.install(input.shaders,p.context);
        if(prepared)p.source_regex.apply(std::move(prepared),p.adapter);
        G->frame_no=unsigned(input.frame);G->gTime=input.seconds;G->gFrameTime=input.frame_seconds;
        if(compute)++p.context.dispatch_number;else ++p.context.draw_number;
        std::vector<CommandList*> pre,post;
        for(unsigned i=compute?5:0;i<(compute?6:5);++i){const auto& shader=p.source_shaders.stage(i);
            if(input.call.hunting_skip)continue;
            if(G->mShaderOverrideMap.empty())continue;
            if(!shader.known || (shader.bound && !shader.hash_known))throw std::runtime_error("Shader matching requires proven original stage hashes");
            const uint64_t hash=shader.bound?shader.hash:0;
            if(i>0 && i<4 && !hash)continue;
            const auto found=lookup_shaderoverride(hash);if(found==G->mShaderOverrideMap.end())continue;
            pre.push_back(&found->second.command_list);post.push_back(&found->second.post_command_list);
        }
        auto call=input.call;auto state=p.command_state();state.call_info=&call;std::wstring why;
        if(!p.adapter.execute_intercepted(pre,post,state,input.original,why))throw std::runtime_error("Original shader call refused: "+utf8(why));
        ++p.stats.shader_calls;return true;
    }catch(const std::exception& e){if(impl_->extension)impl_->extension->execution_failed();reason=e.what();return false;}
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
void RuntimePublishTransitionParameters()
{
    if(!input_adapter || !input_context)throw std::runtime_error("Parameter transition outside active generation");
    CommandListState state;state.mHackerContext=input_context;state.mHackerDevice=input_context->mHackerDevice;
    std::wstring why;
    if(!input_adapter->flush_parameters(state,true,why))throw std::runtime_error("Parameter transition refused: "+utf8(why));
}
bool IniRuntime::present(const RuntimeInput& input, uint64_t frame, float seconds, std::string& reason)
{
    try {
        if(!std::isfinite(input.frame_seconds) || input.frame_seconds<0)throw std::runtime_error("Original frame duration is invalid");
        const auto& w = input.window;
        if (w.client_width < 0 || w.client_height < 0 || w.client_width > 65536 || w.client_height > 65536 ||
            input.resolution_width > 65536 || input.resolution_height > 65536 || !input.resolution_width != !input.resolution_height ||
            input.effective_dpi > 65536 || std::abs(int64_t(input.cursor.hotspot_x)) > 65536 || std::abs(int64_t(input.cursor.hotspot_y)) > 65536)
            throw std::runtime_error("Host window sample is invalid");
#if defined(EFMI_COMMAND_TIMING) || defined(EFMI_PHASE_TIMING)
        auto phase_began = std::chrono::steady_clock::now();
        auto phase = [&](const char* name) {
            const auto now = std::chrono::steady_clock::now();
            impl_->adapter.note_timing(name, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - phase_began).count()));
            phase_began = now;
        };
#else
        auto phase = [](const char*) {};
#endif
        auto& p = *impl_; Scope scope(p.state); phase("present scope"); G->frame_no = unsigned(frame); G->gTime = seconds; G->gFrameTime = input.frame_seconds;
        p.last_input = input; p.state.sample = input;
        G->hWnd = w.present ? RuntimeWindowHandle() : nullptr;
        if (input.resolution_width) { G->mResolutionInfo.width = int(input.resolution_width); G->mResolutionInfo.height = int(input.resolution_height); }
        // HackerSwapChain::RunFrameActions advances the counter before the Present list runs.
        if (input.ticks_us) G->gFPSCounter.Update(input.ticks_us);
        auto sampled = input;
        for (size_t i = 0; i < sampled.keys.size(); ++i) {
            if (!input.foreground || !input.keys[i]) p.blocked_keys[i] = false;
            if (p.blocked_keys[i]) sampled.keys[i] = false;
        }
        input_adapter = &p.adapter; input_context = &p.context;
        if (RuntimeDispatchInput(&p.device, sampled)) ++p.stats.key_events;
        CurrentTransition.UpdatePresets(&p.device);RuntimeUpdateTransitions(&p.device);
        phase("present keys+presets");
        // What replay() starts from: the state after the keys, with what the frame's draws set.
        p.pre_variables.clear(); p.pre_variables.reserve(command_list_globals.size());
        for (auto& item : command_list_globals) p.pre_variables.emplace_back(&item.second, item.second.fval);
        p.pre_parameters = G->iniParams; p.replayable = false;
        assets::FilePlan ignored; std::wstring why;
        DrawPlanAdapter::OriginalCall between;
        if(p.config.shader_hooks)between=[&](CommandListState&,std::wstring&){++G->frame_no;p.context.ResetCallCounters();return true;};
        if (!p.execute(G->present_command_list, G->post_present_command_list, ignored, why,std::move(between)) || !ignored.items.empty())
            throw std::runtime_error("Present requires unavailable render context: " + utf8(why));
        phase("present execute");
        ++p.stats.presents; p.replayable = true; return true;
    } catch (const std::exception& e) {
        if(impl_->extension)impl_->extension->execution_failed();
        reason = e.what(); return false;
    }
}

bool IniRuntime::replay(const RuntimeInput& input, uint64_t frame, float seconds, std::string& reason)
{
    try {
        if(!std::isfinite(input.frame_seconds) || input.frame_seconds<0)throw std::runtime_error("Original frame duration is invalid");
        const auto& w = input.window;
        if (w.client_width < 0 || w.client_height < 0 || w.client_width > 65536 || w.client_height > 65536 ||
            input.resolution_width > 65536 || input.resolution_height > 65536 || !input.resolution_width != !input.resolution_height ||
            input.effective_dpi > 65536 || std::abs(int64_t(input.cursor.hotspot_x)) > 65536 || std::abs(int64_t(input.cursor.hotspot_y)) > 65536)
            throw std::runtime_error("Host window sample is invalid");
        auto& p = *impl_; Scope scope(p.state);
        if (!p.replayable) throw std::runtime_error("No presentation to replay");
        G->frame_no = unsigned(frame); G->gTime = seconds; G->gFrameTime = input.frame_seconds;
        p.state.sample = input;
        G->hWnd = w.present ? RuntimeWindowHandle() : nullptr;
        if (input.resolution_width) { G->mResolutionInfo.width = int(input.resolution_width); G->mResolutionInfo.height = int(input.resolution_height); }
        // The current state comes back whatever the lists do; the lists start from the state the
        // last present() started from, so they draw what it drew.
        struct Restore {
            Impl& p; std::vector<float> now; std::vector<DirectX::XMFLOAT4> parameters;
            Restore(Impl& impl) : p(impl), parameters(G->iniParams)
            {
                now.reserve(p.pre_variables.size());
                for (auto& item : p.pre_variables) { now.push_back(item.first->fval); item.first->fval = item.second; }
                G->iniParams = p.pre_parameters;
            }
            ~Restore()
            {
                for (size_t i = 0; i < p.pre_variables.size(); ++i) p.pre_variables[i].first->fval = now[i];
                G->iniParams = std::move(parameters);
            }
        } restore(p);
        input_adapter = &p.adapter; input_context = &p.context;
        assets::FilePlan ignored; std::wstring why;
        DrawPlanAdapter::OriginalCall between;
        if(p.config.shader_hooks)between=[&](CommandListState&,std::wstring&){++G->frame_no;p.context.ResetCallCounters();return true;};
        if (!p.execute(G->present_command_list, G->post_present_command_list, ignored, why,std::move(between)) || !ignored.items.empty())
            throw std::runtime_error("Present replay requires unavailable render context: " + utf8(why));
        ++p.stats.replays; return true;
    } catch (const std::exception& e) {
        if(impl_->extension)impl_->extension->execution_failed();
        reason = e.what(); return false;
    }
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
    result.commands = p.adapter.command_timing();
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
