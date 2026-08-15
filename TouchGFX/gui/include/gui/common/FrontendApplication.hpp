#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <mvp/MVPApplication.hpp>
#include <touchgfx/Callback.hpp>
#include <gui/model/Model.hpp>

class FrontendHeap;

class FrontendApplication : public touchgfx::MVPApplication
{
public:
    FrontendApplication(Model& model, FrontendHeap& heap);
    virtual ~FrontendApplication() {}
    virtual void changeToStartScreen() { gotoMainScreen(); }
    virtual void handleTickEvent();
    void gotoMainScreen();

private:
    void gotoMainScreenImpl();
    touchgfx::Callback<FrontendApplication> transitionCallback;
    FrontendHeap& frontendHeap;
    Model& model;
};

#endif /* FRONTENDAPPLICATION_HPP */
