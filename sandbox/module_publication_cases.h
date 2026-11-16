#include "../layer/src/character_backend_internal.h"
#include "../layer/src/m2_backend.h"
namespace module_publication_fixture { static unsigned calls=0; }
static void module_publication_cases()
{
    Device device;device.shader_draws=true;device.native_binding_order=true;
    CharacterBackend staged;staged.device=&device;
    shader_hooks_frame(staged,false);
    check(device.shader_draws && device.native_binding_order,"staged preparation cannot replace the active module's dispatch flags");
    device.shader_draws=true;device.native_binding_order=true;
    character_stop(staged);
    check(device.shader_draws && device.native_binding_order,"retiring or rejecting a backend cannot disable the active module's dispatch flags");
    auto active=std::make_shared<CharacterBackend>();active->device=&device;
    ConfigurationProgram program;
    auto hooks=std::make_shared<CharacterBackend::ShaderHooks>();hooks->program=&program;active->shader_hooks=hooks;
    device.shader_draws=false;device.native_binding_order=true;
    device.hunting_overlay=[](Device*,const char*){++module_publication_fixture::calls;};
    device.analysis_frame=[](Device*,const char*,uint32_t){++module_publication_fixture::calls;};
    const auto* module=builtin_character_module();module->activate(*active);
    check(device.shader_draws && !device.native_binding_order,"installation dispatches a root program even without named shader hashes");
    check(!active->frame_calls && !module_publication_fixture::calls,"activation does not simulate Present or publish analysis and overlay callbacks");
    device.character_mode=true;device.character=active;device.character_module=module;device.native_binding_order=true;
    m2_stop(&device);
    check(!device.shader_draws && !device.native_binding_order && !device.character,"host device teardown clears active dispatch flags while detaching the backend");
}
