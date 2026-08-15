#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <mvp/Presenter.hpp>
#include <gui/model/ModelListener.hpp>

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    explicit MainPresenter(MainView& view);
    virtual ~MainPresenter() {}
    virtual void activate();
    virtual void deactivate();

private:
    MainPresenter();
    MainView& view;
};

#endif /* MAINPRESENTER_HPP */
