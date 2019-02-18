// By default, OSX will interpret a long keypress not as a request for repeated
// characters, but as a request to display a accented character menu (but only
// if the character has options, eg, "a" with various accents will have a menu,
// but "b" does not.
//
// This emulator doesn't need this behavior, but it interferes with the desired
// behavior to have repeating characters. It is manafest that keys which do have
// accented char choices will repeat, but those that don't will not repeat.
//
// This issue was hashed out in this wx-user google groups thread:
//
//     https://groups.google.com/forum/#!topic/wx-users/WBG_k9zoI_Y
//
// At this time of this writing, wxWidgets does not expose any function to change
// the preference which controls it, so the user would have to know to type this
// command to disable the accent menu option:
//
//     defaults write -app org.wang2200.www ApplePressAndHoldEnabled -bool false
//
// Instead, this objective-c function is called from the emulator init function
// to establish this preference.

#import <Foundation/Foundation.h>

void osxSetAccentMenuPrefs()
{
    [[NSUserDefaults standardUserDefaults] setObject:@"NO"
                                              forKey:@"ApplePressAndHoldEnabled"];
}
