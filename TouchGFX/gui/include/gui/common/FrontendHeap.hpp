#ifndef FRONTENDHEAP_HPP
#define FRONTENDHEAP_HPP

#include <common/Meta.hpp>
#include <common/Partition.hpp>
#include <mvp/MVPHeap.hpp>
#include <touchgfx/transitions/NoTransition.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/main_screen/MainView.hpp>
#include <gui/model/Model.hpp>

class FrontendHeap : public touchgfx::MVPHeap
{
public:
    typedef touchgfx::meta::TypeList<MainView, touchgfx::meta::Nil> ViewTypes;
    typedef touchgfx::meta::TypeList<MainPresenter, touchgfx::meta::Nil> PresenterTypes;
    typedef touchgfx::meta::TypeList<touchgfx::NoTransition, touchgfx::meta::Nil> TransitionTypes;

    static FrontendHeap& getInstance()
    {
        static FrontendHeap instance;
        return instance;
    }

    touchgfx::Partition<PresenterTypes, 1> presenters;
    touchgfx::Partition<ViewTypes, 1> views;
    touchgfx::Partition<TransitionTypes, 1> transitions;
    FrontendApplication app;
    Model model;

private:
    FrontendHeap()
        : MVPHeap(presenters, views, transitions, app),
          presenters(), views(), transitions(), app(model, *this), model()
    {
        app.gotoMainScreen();
    }
};

#endif /* FRONTENDHEAP_HPP */
