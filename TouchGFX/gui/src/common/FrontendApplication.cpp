#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/main_screen/MainView.hpp>
#include <touchgfx/transitions/NoTransition.hpp>

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : touchgfx::MVPApplication(), transitionCallback(),
      frontendHeap(heap), model(m)
{
}

void FrontendApplication::handleTickEvent()
{
    model.tick();
    touchgfx::MVPApplication::handleTickEvent();
}

void FrontendApplication::gotoMainScreen()
{
    transitionCallback = touchgfx::Callback<FrontendApplication>(
        this, &FrontendApplication::gotoMainScreenImpl);
    pendingScreenTransitionCallback = &transitionCallback;
}

void FrontendApplication::gotoMainScreenImpl()
{
    touchgfx::makeTransition<MainView, MainPresenter, touchgfx::NoTransition, Model>(
        &currentScreen, &currentPresenter, frontendHeap,
        &currentTransition, &model);
}
