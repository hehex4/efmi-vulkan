// Native XXMI frontend prototype. No game, hooks, window or graphics device.
#include "CommandList.h"
#include "globals.h"
#include "HackerContext.h"
#include "IniHandler.h"
#include "input.h"
#include <cstdio>
#include <stdexcept>

Globals host_globals;
Globals* G=&host_globals;
FILE* LogFile=nullptr;
bool gLogDebug=false;
LogVerbosity gLogVerbosity=LogVerbosity::INVALID;

void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostCompileList(const wchar_t*,CommandList*,CommandList*);
void HostParseConstants();
size_t HostInputCount();
void HostSyntheticInput(bool);
class HostListener:public InputListener {
public:
    int downs=0,ups=0;
    void DownEvent(HackerDevice*) override { ++downs; }
    void UpEvent(HackerDevice*) override { ++ups; }
};
static int passed=0,failed=0;
static void check(bool ok,const char* message) { if(ok){++passed;printf("PASS: %s\n",message);}else {++failed;printf("FAIL: %s\n",message);} }
static float expression(const wchar_t* text,CommandListState* state=nullptr)
{
    std::wstring input=text,ns;CommandListExpression expr;
    if(!expr.parse(&input,&ns,nullptr))throw std::runtime_error("expression parse");
    if(state)return expr.evaluate(state);
    float result=0;
    if(!expr.static_evaluate(&result))throw std::runtime_error("expression is not constant");
    return result;
}
static void execute(CommandList& list,CommandListState& state)
{
    for(auto& command:list.commands)command->run(&state);
}
int wmain()
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout,nullptr,_IONBF,0);
    InitializeCriticalSection(&G->mCriticalSection);
    InitializeCriticalSection(&G->mResourcesLock);
    extern CRITICAL_SECTION resource_creation_mode_lock;
    InitializeCriticalSection(&resource_creation_mode_lock);
    G->user_config=L"host-test-user-settings.ini";
    HackerContext context;CommandListState state;state.mHackerContext=&context;
    try {
        check(expression(L"2 + 3 * 4")==14,"original arithmetic precedence");
        check(expression(L"(2 + 3) * 4")==20,"original parentheses");
        check(expression(L"2 < 3 && 7 == 7")==1,"original comparisons and boolean operators");
        check(expression(L"17 % 5")==2,"original remainder");
        HostParseExcerpt(L"[Constants]\nglobal $result = 0\nglobal $mode = 0\n"
                         L"[CommandListCompute]\n$result = 2\nif $mode == 0\n$result = $result * 3\n"
                         L"else\n$result = 9\nendif\npost $result = $result + 1\n",nullptr);
        CommandList constants,pre,post;
        HostParseConstants();
        HostCompileList(L"Constants",&constants,nullptr);
        execute(constants,state);
        HostCompileList(L"CommandListCompute",&pre,&post);
        optimise_command_lists(nullptr);
        execute(pre,state);
        check(expression(L"$result",&state)==6,"source-ordered assignment and true branch");
        state.post=true;execute(post,state);state.post=false;
        check(expression(L"$result",&state)==7,"post commands execute separately");
        command_list_globals.at(L"$mode").fval=1;execute(pre,state);
        check(expression(L"$result",&state)==9,"variable change selects else branch without reparsing");
        CustomResourcePool pool;pool.name=L"PoolTest";
        pool.variable_template=std::make_unique<CommandListVariable>(L"PoolTest",3,VariableFlags::NONE);
        pool.Initialize(2);
        auto* a=pool.GetVariable(0,false,true,true);auto* b=pool.GetVariable(1,false,true,true);
        check(a && b && a!=b && a->fval==3 && b->fval==3,"pool lazily creates independent variables from template");
        a->fval=19;
        check(pool.GetVariable(2,false,true,false)==a && pool.GetVariable(-1,false,true,false)==b,"ring pool wraps positive and negative indices");
        CustomResourcePool alias;check(alias.SetSourcePool(&pool) && alias.GetVariable(0,false,true,false)==a,"pool reference shares live content");
        check(!pool.SetSourcePool(&alias),"pool reference rejects a cycle");
        CommandList one,two,alias_list;one.commands=pre.commands;two.commands=post.commands;
        check(alias_list.SetSourceCommandList(&one) && alias_list.ResolveCommandList()==&one,"callback binds a command list");
        check(alias_list.SetSourceCommandList(&two) && alias_list.ResolveCommandList()==&two,"callback can be reassigned without rebuilding lists");
        check(!two.SetSourceCommandList(&alias_list),"callback rejects a cycle");
        HostParseExcerpt(L"[Present]\n$result = $result + 4\n[Present]\n$result = $result + 5\n",nullptr);
        CommandList present,present_post;HostCompileList(L"Present",&present,&present_post);
        optimise_command_lists(nullptr);execute(present,state);
        check(expression(L"$result",&state)==13,"root-file duplicate Present keeps only the first section");
        HostParseExcerpt(L"[Present]\n$result = $result + 7\n",L"included-a.ini");
        HostParseExcerpt(L"[Present]\n$result = $result + 11\n",L"included-b.ini");
        CommandList appended,appended_post;HostCompileList(L"Present",&appended,&appended_post);
        optimise_command_lists(nullptr);
        command_list_globals.at(L"$result").fval=0;execute(appended,state);
        check(expression(L"$result",&state)==22,"included global Present sections append in parse order");
        CustomResourcePool fifo;fifo.name=L"PoolExpiring";fifo.index_type=PoolIndexType::FIFO;
        fifo.expiration_timeout_frames=2;fifo.reset_expired_elements=true;
        fifo.variable_template=std::make_unique<CommandListVariable>(L"PoolExpiring",3,VariableFlags::NONE);
        G->frame_no=10;fifo.Initialize(2);
        fifo.GetVariable(81,false,false,true)->fval=42;
        G->frame_no=12;check(fifo.GetVariable(81,false,false,false)->fval==42,"pool entry survives at expiration boundary");
        G->frame_no=13;check(fifo.GetVariable(81,false,false,false)->fval==3,"expired pool entry reads template value");
        std::wstring ns;CommandListScope scope;scope.emplace_front();
        ResourceCopyTarget region;
        check(region.ParseTarget(L"vs-cb2->region(16 + 32, 256)",true,&ns,&scope) &&
              region.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE_REGION && region.slot==2 &&
              region.member_args[0].GetValue(&state)==48 && region.member_args[1].GetValue(&state)==256,
              "Region preserves evaluated byte offset and length");
        CommandList formats;formats.scope=&scope;std::wstring format=L"r16g16b16a16_uint";
        bool format_ok=ParseCommandListResourceCopyTargetDirective(L"CommandListFormat",L"vb2->elementformat(blendindices, 0)",
                                                                   &format,&formats,&ns);
        auto operation=formats.commands.empty()?nullptr:dynamic_cast<LayoutElementOperation*>(formats.commands[0].get());
        if(operation)operation->run(&state);
        check(format_ok && operation && operation->override.match.input_slot==2 &&
              operation->override.match.semantic_name=="BLENDINDICES" && operation->override.match.semantic_index==0 &&
              operation->override.replace.format==DXGI_FORMAT_R16G16B16A16_UINT &&
              state.input_layout_overrides.size()==1 && state.input_layout_overrides[0]==&operation->override,
              "ElementFormat resolves its semantic index at execution without drawing");
        auto first=std::make_shared<HostListener>(),second=std::make_shared<HostListener>();
        auto input_count=HostInputCount();
        RegisterKeyBinding(L"Key",L"F12",first,0,0,0);RegisterKeyBinding(L"Key",L"F12",second,0,0,0);
        check(HostInputCount()==input_count+2,"duplicate key listeners are both registered");
        HostSyntheticInput(true);HostSyntheticInput(true);HostSyntheticInput(false);
        check(first->downs==1 && second->downs==1 && first->ups==1 && second->ups==1,
              "both listeners receive one synthetic edge without real keyboard input");
        bool refused=false;
        try { context.Bind3DMigotoResources(); } catch(const std::runtime_error&) { refused=true; }
        check(refused,"unimplemented GPU adapter explicitly refuses execution");
        ClearKeyBindings();
    } catch(const std::exception& e) {printf("FAILED: %s\n",e.what());++failed;}
    printf("XXMI hosted CPU prototype: %d checks passed, %d failed\n",passed,failed);
    return failed?1:0;
}
