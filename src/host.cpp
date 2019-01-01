// ============================================================================
// headers
// ============================================================================

#include "Ui.h"                 // pick up debugging def of "new", but make this cleaner
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "host.h"

#include "wx/filename.h"        // for wxFileName
#include "wx/fileconf.h"        // for configuration state object
#include "wx/tokenzr.h"         // req'd by wxStringTokenizer
#include "wx/settings.h"        // req'd by GetMetric()
#include "wx/utils.h"           // time/date stuff
#include "wx/stdpaths.h"        // wxStandardPaths stuff

// ============================================================================
// module state
// ============================================================================

static std::string                   app_home;   // path to application home directory
static std::unique_ptr<wxFileConfig> config;     // configuration file object
static std::unique_ptr<wxStopWatch>  stopwatch;  // time program started

// remember where certain files are located
struct file_group_t {
    std::string  dir;         // dir where files come from
    std::string  name;        // most recently chosen
    std::string  filter;      // suffix filter string
    int          filter_idx;  // which filter was chosen
    std::string  ini_group;   // name in .ini file
};

static file_group_t file_group[host::FILEREQ_NUM];

// ============================================================================
// file-local functions
// ============================================================================

// get information about default dirs for file categories
static void
getConfigFileLocations()
{
    std::string subgroup("..");
    std::string foo;

    bool b = host::configReadStr(subgroup, "configversion", &foo);
    if (b && (foo != "1")) {
        UI_warn("Configuration file version '%s' found.\n"
                 "Version '1' expected.\n"
                 "Attempting to read the config file anyway.\n", foo.c_str());
    }

    for (auto &group : file_group) {
        subgroup = group.ini_group;
        long v = 0;

        b = host::configReadStr(subgroup, "directory", &foo);
        group.dir = (b) ? foo : ".";

        b = host::configReadStr(subgroup, "filterindex", &foo);
        group.filter_idx = (b && wxString(foo).ToLong(&v)) ? v : 0;

        group.name = "";     // intentionally don't save this
    }
}


// save information about default dirs for file categories
static void
saveConfigFileLocations()
{
    std::string subgroup("..");
    std::string version("1");

    host::configWriteStr(subgroup, "configversion", version);

    for (auto const &group : file_group) {
        subgroup = group.ini_group;
        host::configWriteStr(subgroup, "directory",   group.dir);
        host::configWriteInt(subgroup, "filterindex", group.filter_idx);
    }
}


// ------------------------------------------------------------------------
//  a small logging facility, not in the host namespace
// ------------------------------------------------------------------------

#include <cstdarg>      // for var args
#include <fstream>

static std::ofstream dbg_ofs;

#ifdef _DEBUG
static void
dbglog_open(const std::string &logname)
{
    assert(!dbg_ofs.is_open());     // only one log at a time
    dbg_ofs.open(logname.c_str(), std::ofstream::out | std::ofstream::trunc);
    if (!dbg_ofs.good()) {
        UI_error("Error opening '%s' for logging.\n", logname.c_str());
        exit(-1);
    }
}


static void
dbglog_close()
{
    if (dbg_ofs.is_open()) {
        dbg_ofs.close();
    }
}
#endif

void
dbglog(const char *fmt, ...)
{
    char buff[1000];
    va_list args;

    va_start(args, fmt);
    vsnprintf(&buff[0], sizeof(buff), fmt, args);
    va_end(args);

    if (dbg_ofs.good()) {
        dbg_ofs << &buff[0];
        // this is useful if we are getting assert()s, causing
        // the last buffered block to not appear in the log.
        dbg_ofs.flush();
    }
}


// ============================================================================
// "public" functions
// ============================================================================

void
host::initialize()
{
#ifdef _DEBUG
    dbglog_open("w2200dbg.log");
#endif

    // path to executable
    const wxStandardPathsBase &stdp = wxStandardPaths::Get();
    wxFileName exe_path(stdp.GetExecutablePath());

#ifdef __VISUALC__
    // with ms visual c++, there is a Debug directory and a Release directory.
    // these are one below the anticipated real location where the exe will
    // live, so if we detect we are running from there, we raise the directory
    // one notch.
    const int dircount = exe_path.GetDirCount();
    const wxArrayString dirnames = exe_path.GetDirs();
    if (dirnames[dircount-1].Lower() == "debug" ||
        dirnames[dircount-1].Lower() == "release") {
        exe_path.AppendDir("..");
        exe_path.Normalize();
    }
#endif

#ifdef __WXMAC__
  #if 1
    // the mac bundle wants references to the Resources directory,
    // not the directory where the application lives
    exe_path.AppendDir("..");
    exe_path.AppendDir("Resources");
    exe_path.Normalize();
  #else
    exe_path = stdp.GetResourcesDir();
  #endif
#endif

    app_home = std::string(exe_path.GetPath(wxPATH_GET_VOLUME));

#ifdef __WXMSW__
    config = std::make_unique<wxFileConfig>(
                wxEmptyString,                  // appName
                wxEmptyString,                  // vendorName
                app_home + "\\wangemu.ini",     // localFilename
                wxEmptyString,                  // globalFilename
                wxCONFIG_USE_LOCAL_FILE
             );
#elif defined(__WXMAC__)
  #if 1
    // put wangemu.ini file in same directory as the executable
    // (on the Mac it lives under ~/Library/Preferences)
    wxFileName ini_path("~/Library/Preferences/wangemu.ini");
    ini_path.Normalize();
  #else
    wxFileName init_path(stdp.GetUserConfigDir() + "/wangemu.ini");
  #endif
    config = std::make_unique<wxFileConfig>("", "", ini_path.GetFullPath());
    wxConfigBase::Set(config);
#endif

    // needed so we can compute a time difference to get ms later
    stopwatch = std::make_unique<wxStopWatch>();
    stopwatch->Start(0);

    // default file locations
    file_group[FILEREQ_SCRIPT] = {
        ".",                            // dir
        "",                             // name
        "script files (*.w22)"          // filter
        "|*.w22|text files (*.txt)"
        "|*.txt|All files (*.*)|*.*",
        0,                              // filter_idx
        "ui/script"                     // ini_group
    };

    file_group[FILEREQ_GRAB] = {
        ".",                            // dir
        "",                             // name
        "BMP (*.bmp)|*.bmp"             // filter
        "|Any file (*.*)|*.*",
        0,                              // filter_idx
        "ui/screengrab"                 // ini_group
    };

    file_group[FILEREQ_DISK] = {
        ".",                                // dir
        "",                                 // name
        "wang virtual disk (*.wvd)|*.wvd"   // filter
        "|All files (*.*)|*.*",
        0,                                  // filter_idx
        "ui/disk"                           // ini_group
    };

    file_group[FILEREQ_PRINTER] = {
        ".",                                // dir
        "",                                 // name
        "Text Files (*.txt)|*.txt"          // filter
        "|All files (*.*)|*.*",
        0,                                  // filter_idx
        "ui/printer"                        // ini_group
    };

    // now try and read in defaults from ini file
    getConfigFileLocations();
}


// host is a kind of singleton, and as such it isn't owned and thus isn't
// destroyed by going out of scope.  Instead, TheApp::OnExit() calls this
// from UiSystem.cpp.
void
host::terminate()
{
    if (config) {
        saveConfigFileLocations();
    }
    config    = nullptr;
    stopwatch = nullptr;

#ifdef _DEBUG
    dbglog_close();       // turn off logging
#endif
}


// ----------------------------------------------------------------------------
// uniform file dialog
// ----------------------------------------------------------------------------
// ask user to provide a file location

int
host::fileReq(int requestor, const std::string &title, bool readonly, std::string *fullpath)
{
    assert(fullpath != nullptr);
    assert(requestor >= 0 && requestor < FILEREQ_NUM);

    const int style = (readonly) ? (wxFD_OPEN | wxFD_FILE_MUST_EXIST)
                                 : (wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    // get the name of a file to execute
    wxFileDialog dialog(
            nullptr,
            title.c_str(),
            file_group[requestor].dir,       // default directory
            file_group[requestor].name,      // default file
            file_group[requestor].filter,    // file suffix filter
            style);
    dialog.SetFilterIndex(file_group[requestor].filter_idx);

    if (dialog.ShowModal() == wxID_OK) {
        // remember what and where we selected
        file_group[requestor].dir       = dialog.GetDirectory();
        file_group[requestor].name      = dialog.GetFilename();
        file_group[requestor].filter_idx = dialog.GetFilterIndex();
        *fullpath = dialog.GetPath();
        return FILEREQ_OK;
    }
    return FILEREQ_CANCEL;
}


// return the absolute path to the dir containing the app
std::string host::getAppHome()
{
    return app_home;
}


// classifies the supplied filename as being either relative (false)
// or absolute (returns true).
bool
host::isAbsolutePath(const std::string &name)
{
    wxFileName fname(name);
    return fname.IsAbsolute();
}


// make sure the name is put in normalized format
std::string
host::asAbsolutePath(const std::string &name)
{
    wxFileName fn(name);
    fn.MakeAbsolute();
    std::string rv(fn.GetFullPath());
    return rv;
}


// ----------------------------------------------------------------------------
// Application configuration storage
// ----------------------------------------------------------------------------

// fetch an association from the configuration file
bool
host::configReadStr(const std::string &subgroup,
                    const std::string &key,
                    std::string *val,
                    const std::string *defaultval)
{
    assert(val != nullptr);
    wxString wxval;
    config->SetPath("/wangemu/config-0/" + subgroup);
    bool b = config->Read(key, &wxval);
    if (!b && (defaultval != nullptr)) {
        *val = *defaultval;
    } else {
        *val = wxval;
    }
    return b;
}


bool
host::configReadInt(const std::string &subgroup,
                    const std::string &key,
                    int *val,
                    const int defaultval)
{
    assert(val != nullptr);
    std::string valstr;
    bool b = configReadStr(subgroup, key, &valstr);
    long v = 0;
    if (b) {
        wxString wxv(valstr);
        b = wxv.ToLong(&v, 0);  // 0 means allow hex and octal notation too
    }
    *val = (b) ? (int)v : defaultval;
    return b;
}


void
host::configReadBool(const std::string &subgroup,
                     const std::string &key,
                     bool *val,
                     const bool defaultval)
{
    assert(val != nullptr);
    int v = 0;
    const bool b = configReadInt(subgroup, key, &v, ((defaultval) ? 1:0));
    if (b && (v >= 0) && (v <= 1)) {
        *val = (v==1);
    } else {
        *val = defaultval;
    }
}


// read the geometry for a window and if it is reasonable, use it, otherwise
// use the supplied default (if any).  the reason for the client_size control
// is that all the windows used client size, but for some reason, the
// printpreview window would shrink vertically on each save/restore cycle
// by 20 pixels.  using GetSize/SetSize fixed that problem for printpreview.
// It would have been OK for every window to use that instead I suppose, but
// for people who already had a .ini file, it would have messed up their
// settings a bit on the changeover.
void
host::configReadWinGeom(wxWindow *wxwin,
                        const std::string &subgroup,
                        wxRect * const default_geom,
                        bool client_size)
{
    long x=0, y=0, w=0, h=0;    // just for lint
    std::string valstr;

    bool b = configReadStr(subgroup, "window", &valstr);
    if (b) {
        wxStringTokenizer stkn(valstr, ",");
        b = (stkn.CountTokens() == 4);  // there should be four items
        if (b) {
            wxString str_x = stkn.GetNextToken();
            wxString str_y = stkn.GetNextToken();
            wxString str_w = stkn.GetNextToken();
            wxString str_h = stkn.GetNextToken();
            b = str_x.IsNumber() && str_y.IsNumber() &&
                str_w.IsNumber() && str_h.IsNumber();
            if (b) {
                str_x.ToLong(&x, 0);
                str_y.ToLong(&y, 0);
                str_w.ToLong(&w, 0);
                str_h.ToLong(&h, 0);
            }
        }
    }

    if (!b) {
        // the specified geometry was bad; use the supplied default
        if (default_geom == nullptr) {
            return;     // nothing we can do
        }
        x = default_geom->GetX();
        y = default_geom->GetY();
        w = default_geom->GetWidth();
        h = default_geom->GetHeight();
    }

    // sanity check window position
    const int screen_w = wxSystemSettings::GetMetric(wxSYS_SCREEN_X);
    const int screen_h = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);

    // if origin is off screen, move it on screen
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if ((x > screen_w-4) || (y > screen_h-4)) {  // at least a tiny nub must show
        return;
    }

    // don't let the window be bigger than the screen
    if (w > screen_w) {
        w = screen_w;
    }
    if (h > screen_h) {
        h = screen_h;
    }

    // now move and resize the window
    const wxPoint pt(x, y);
    const wxSize  sz(w, h);
    wxwin->Move(pt);
    if (client_size) {
        wxwin->SetClientSize(sz);
    } else {
        wxwin->SetSize(sz);
    }
}


// send a string association to the configuration file
void
host::configWriteStr(const std::string &subgroup,
                     const std::string &key,
                     const std::string &val)
{
    wxString wxKey(key);
    wxString wxVal(val);
    config->SetPath("/wangemu/config-0/" + subgroup);
    const bool b = config->Write(wxKey, wxVal);
    assert(b);
}


// send an integer association to the configuration file
void
host::configWriteInt(const std::string &subgroup,
                     const std::string &key,
                     const int val)
{
    wxString foo;
    foo.Printf("%d", val);
    configWriteStr(subgroup, key, std::string(foo));
}


// send a boolean association to the configuration file
void
host::configWriteBool(const std::string &subgroup,
                      const std::string &key,
                      const bool val)
{
    const int foo = (val) ? 1 : 0;
    configWriteInt(subgroup, key, foo);
}


// write out the geometry for a window
void
host::configWriteWinGeom(wxWindow *wxwin,
                         const std::string &subgroup,
                         bool client_size)
{
    assert(wxwin != nullptr);

    int x=0, y=0, w=0, h=0;
    wxwin->GetPosition(&x, &y);
    if (client_size) {
        wxwin->GetClientSize(&w, &h);
    } else {
        wxwin->GetSize(&w, &h);
    }

    wxString prop;
    prop.Printf("%d,%d,%d,%d", x, y, w, h);
    configWriteStr(subgroup, "window", std::string(prop));
}


// ----------------------------------------------------------------------------
// real time functions
// ----------------------------------------------------------------------------

// return the time in milliseconds as a 64b signed integer
int64
host::getTimeMs()
{
    // newer api should provide more accurate measurement of time
    // NB: wxLongLong can't be mapped directly to "long long" type,
    //     thus the following gyrations
    const wxLongLong x_time_us = stopwatch->TimeInMicro();
    const uint32 x_low     = x_time_us.GetLo();
    const  int32 x_high    = x_time_us.GetHi();
    const  int64 x_time_ms = (((int64)x_high << 32) | x_low) / 1000;
    return x_time_ms;
}


// go to sleep for approximately ms milliseconds before returning
void
host::sleep(unsigned int ms)
{
    wxMilliSleep(ms);
}

// vim: ts=8:et:sw=4:smarttab
