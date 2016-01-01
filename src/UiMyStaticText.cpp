
#include "UiSystem.h"           // sharing info between Ui* wxgui modules
#include "UiMyStaticText.h"     // sharing info between UI_wxgui modules


// dialog events to catch
BEGIN_EVENT_TABLE(MyStaticText, wxStaticText)
    EVT_KEY_DOWN    ( MyStaticText::OnKeyDown )
    EVT_LEFT_DOWN   ( MyStaticText::OnMouseClick )
    EVT_MIDDLE_DOWN ( MyStaticText::OnMouseClick )
    EVT_RIGHT_DOWN  ( MyStaticText::OnMouseClick )
END_EVENT_TABLE()


MyStaticText::MyStaticText(wxWindow* parent, wxWindowID id,
                                        const wxString &label)
    : wxStaticText(parent, id, label)
{
    // nothing to do!
}


// redirect mouse clicks to parent dialog
void MyStaticText::OnMouseClick(wxMouseEvent &event)
{
    GetParent()->GetEventHandler()->AddPendingEvent(event);
}


// make RETURN key kill the dialog too
void MyStaticText::OnKeyDown(wxKeyEvent &event)
{
    int wxKey = event.GetKeyCode();
    if (wxKey == WXK_RETURN) {
        // fabricate a left click and send it to the dialog
        // this will cause it to shut down
        wxMouseEvent evt(wxEVT_LEFT_DOWN);
        GetParent()->GetEventHandler()->AddPendingEvent(evt);
    } else {
        event.Skip();
    }
}

// vim: ts=8:et:sw=4:smarttab
