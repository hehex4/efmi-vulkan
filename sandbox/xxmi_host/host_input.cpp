// Test-only button state; never polls or sends input to the desktop/game.
#include "input.cpp"
class HostButton:public InputButton {
public:
    bool down=false;
    bool CheckState() override { return down; }
};
size_t HostInputCount() { return actions.size(); }
void HostSyntheticInput(bool down)
{
    for(auto* action:actions){
        auto* button=dynamic_cast<HostButton*>(action->button);
        if(!button){delete action->button;button=new HostButton;action->button=button;}
        button->down=down;action->Dispatch(nullptr);
    }
}
