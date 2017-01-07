// This class encapsulates non-gui, host-dependent services:
//    configuration persistence
//    file access
//    real time functions
//
// It is designed as a singleton (really the "borg" idiom).  To use it,
// create a Host instance anywhere and use its services, eg
//    Host hst;
//    m_simtime = hst.getTimeMs();
// or
//    m_simtime = Host().getTimeMs();
//
// The configuration is kept in an .ini file, with data stored hierarchically.
// Viewed like a scoping problem, there are a few global ini values that
// describe the ini file format revision.  Then there are N sets of emulator
// configuration state; presently things are hardwired to have just a single
// set of state.  Perhaps in the future the emulator will allow saving and
// restoring multiple sets.  Within a given set of state, there are some
// settings for the CPU type, the amount of RAM, speed regulation.  Beneath
// that, there are M sets of card state, one for each I/O slot.  There are
// some standard parts to describe what kind of card is plugged into each
// slot, but the rest of the per-card ini state depends on the type of card.
//
// All the Config* functions take as a first parameter the "subgroup", which
// is a concatenation of the ini storage path up until a final set of state.
// The "key" is the final level of lookup.  There is nothing special about it;
// the interface could have required the caller to send the subgroup+key, but
// this way seemed to save a bit of code at the call point.
//
// The emulator has need to save and load files for various reasons.  The
// emulator keeps a separate history of file access for each class.  Thus,
// if you keep disk images in one directory and want to save script files
// in another, there is no need to keep navigating back and forth between
// them.

#ifndef _INCLUDE_HOST_H_
#define _INCLUDE_HOST_H_

#include "w2200.h"

class wxWindow;
class wxRect;
class wxFileConfig;
class wxStopWatch;

class Host
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Host);
    Host();
    ~Host();

    // this should be called at the end of the world to really free resources.
    // this could be avoided by creating/destroying m_config object on each
    // new Host, but that is excessive churn during start up / shut down, no?
    static void terminate();

    // ---- read or write an entry in the configuration file ----
    // there are keys maintained for separate categories.
    // the ConfigRead* functions take a defaultval; this is the value returned
    // if the key for that subgroup isn't found in the config file.

    static bool ConfigReadStr(  const std::string &subgroup,
                                const std::string &key,
                                std::string *val,
                                const std::string *defaultval = 0);

    static void ConfigWriteStr( const std::string &subgroup,
                                const std::string &key,
                                const std::string &val);

    static bool ConfigReadInt(  const std::string &subgroup,
                                const std::string &key,
                                int *val,
                                const int defaultval = 0);

    static void ConfigWriteInt( const std::string &subgroup,
                                const std::string &key,
                                const int val);

    static void ConfigReadBool( const std::string &subgroup,
                                const std::string &key,
                                bool *val,
                                const bool defaultval = false);

    static void ConfigWriteBool(const std::string &subgroup,
                                const std::string &key,
                                const bool val);

    // these are specialized for saving/recalling the location and size
    // of windows of various kinds: emulated terminals, dialog boxes.
    // the client_size flag indicates whether the geometry we are dealing
    // with is of the window or the client.
    static void ConfigReadWinGeom(  wxWindow     *wxwin,
                                    const std::string &subgroup,
                                    wxRect       *default_geom = nullptr,
                                    bool          client_size = true );

    static void ConfigWriteWinGeom( wxWindow     *wxwin,
                                    const std::string &subgroup,
                                    bool          client_size = true );

    // ---- time functions ----

    // return the time in milliseconds as a 64b signed integer
    static int64 getTimeMs(void);

    // go to sleep for approximately ms milliseconds before returning
    static void sleep(unsigned int ms);

    // ---- file path functions ----

    // classifies the supplied filename as being either relative (false)
    // or absolute (returns true).
    static bool isAbsolutePath(const std::string &name);

    // make sure the name is put in normalized format
    static std::string asAbsolutePath(const std::string &name);

    // return the absolute path to the dir containing the app
    static std::string getAppHome();

    // ---- ask user to provide a file location ----
    // categories of separate file locations
    enum { FILEREQ_SCRIPT,  // for .w22 script files
           FILEREQ_GRAB,    // for screen grabs
           FILEREQ_DISK,    // for floppy disk directory
           FILEREQ_PRINTER, // for printer output
           FILEREQ_NUM,     // number of filereq types
         };

    // return status
    enum { FILEREQ_OK, FILEREQ_CANCEL };

    // for a given category (FILEREQ_*), ask to select a file from
    // the default directory for that category.
    static int fileReq(int requestor, std::string title,
                       int readonly, std::string *fullpath);

private:
    // initialize class members
    static void initMembers();

    // get/save information about default dirs for file categories
    static void  getConfigFileLocations();
    static void saveConfigFileLocations();

    // set up the object the first time it is used
    static bool m_initialized;

    static std::string                   m_app_home;  // path to application home directory
    static std::unique_ptr<wxFileConfig> m_config;    // configuration file object
    static std::unique_ptr<wxStopWatch>  m_stopwatch; // real time since simulation started

    // remember where certain files are located
    static std::string m_FileDir[FILEREQ_NUM];         // dir where files come from
    static std::string m_Filename[FILEREQ_NUM];        // most recently chosen
    static std::string m_FileFilter[FILEREQ_NUM];      // suffix filter string
    static int         m_FileFilterIdx[FILEREQ_NUM];   // which filter was chosen
    static std::string m_IniGroup[FILEREQ_NUM];        // name in .ini file
};

#endif _INCLUDE_HOST_H_

// vim: ts=8:et:sw=4:smarttab
