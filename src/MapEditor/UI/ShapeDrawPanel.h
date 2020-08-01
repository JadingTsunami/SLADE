#pragma once

#define MIN_ELLIPSE_SIDES (3)
#define MAX_ELLIPSE_SIDES (1000)
class wxBoxSizer;

class ShapeDrawPanel : public wxPanel
{
public:
	ShapeDrawPanel(wxWindow* parent);
	~ShapeDrawPanel() {}

	void showShapeOptions(int shape);
    void setActiveShape(int shape);
    void setCentered(bool v);
    void setRatioLock(bool v);
    void offsetSides(int offset);


private:
	wxChoice*	choice_shape_	= nullptr;
	wxCheckBox*	cb_centered_	= nullptr;
	wxCheckBox*	cb_lockratio_	= nullptr;
	wxBoxSizer*	sizer_main_		= nullptr;
	wxSpinCtrl*	spin_sides_		= nullptr;
	wxPanel*	panel_sides_	= nullptr;
};
