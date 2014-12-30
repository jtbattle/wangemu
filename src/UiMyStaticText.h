// ----------------------------------------------------------------------------
// customize the static text control to allow redirecting mouse clicks
// to the dialog's mouse handler.  without this, any mouse clicks in the
// bounding box of the static text control just get silently absorbed.
// ----------------------------------------------------------------------------

#ifndef _INCLUDE_UI_MYSTATICTEXT_H_
#define _INCLUDE_UI_MYSTATICTEXT_H_

class MyStaticText : public wxStaticText
{
public:
     MyStaticText(wxWindow* parent, wxWindowID id, const wxString& label);
private:
    void OnKeyDown(wxKeyEvent& event);
    void OnMouseClick(wxMouseEvent& event);

    CANT_ASSIGN_OR_COPY_CLASS(MyStaticText);
    DECLARE_EVENT_TABLE()
};

#endif // _INCLUDE_UI_MYSTATICTEXT_H_
