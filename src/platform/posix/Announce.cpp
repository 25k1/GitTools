#include "ui/Announce.hpp"

#include "util/Encoding.hpp"

#include <gtk/gtk.h>

#include <wx/window.h>

#include <string>

namespace git_tools {

namespace {

constexpr gint kLivePolite = 1;

GtkWidget* AnnouncingWidget(wxWindow* window) {
    GtkWidget* widget = window->GetHandle();
    if (!widget) return nullptr;
    GtkWidget* top = gtk_widget_get_toplevel(widget);
    GtkWidget* focus =
        top && GTK_IS_WINDOW(top) ? gtk_window_get_focus(GTK_WINDOW(top)) : nullptr;
    return focus ? focus : widget;
}

}

void PrepareAnnouncements(wxWindow*) {}

void Announce(wxWindow* window, const std::wstring& text) {
    GtkWidget* widget = window ? AnnouncingWidget(window) : nullptr;
    AtkObject* accessible = widget ? gtk_widget_get_accessible(widget) : nullptr;
    if (!accessible) return;

    const std::string message = WideToUtf8(text);
    if (g_signal_lookup("notification", ATK_TYPE_OBJECT) != 0) {
        g_signal_emit_by_name(accessible, "notification", message.c_str(), kLivePolite);
    } else if (g_signal_lookup("announcement", ATK_TYPE_OBJECT) != 0) {
        g_signal_emit_by_name(accessible, "announcement", message.c_str());
    }
}

}
