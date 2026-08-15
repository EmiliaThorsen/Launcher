# Launcher
Clay-based app launcher mimicking the GNOME/Android grid launcher style.

Config of how folders are structured and more is in `~/.config/launcher.conf`.

## Config
Config re-uses the same parsing code as the desktop file parser, so the format is very similar.
It starts with a `[Launcher]` block; after this you can set:
* rows and columns of the main grid using `Columns=` and `Rows=`
* the size of all icons on screen; this uses the appropriate size from your icon directories according to spec. Use `IconSize=`
* space between icons in pixels with `HGap=` and `VGap=`
* font size of the text under icons using `FontSize=`
* font name using `FontName=`
* the max length before app names truncate, using `NameMaxLen=`
* the terminal emulator you want to launch terminal-based apps in, using `Terminal=`; only tested with kitty so far

All values mentioned except `FontName` and `Terminal` expect a plain integer after the key. The two keys that expect a string — font family and terminal name — do not need quotes.

After the initial block you can freely define folders. The block name is of the form `[folderN]`, where N is one or more digits. Folders contain a few config lines and then a list of apps.
* `Name=` defines what the folder will be listed as, same as an app's name shown under its icon.
* `Icon=` is an XDG icon name specifying the icon the folder shows when closed.
* `Rows=` and `Columns=`, same as the main config, set how big the grid is when you open the folder.
* `Apps=` is the list of desktop file names (without the `.desktop` extension) to make apps appear in the folder.

Finally, to get it all to work you define pages. A page is another block of the form `[pageN]`. N can be any number you want, but pages are shown in the order they appear in the config file, not sorted by N. Pages don't have names beyond their position, so there's no name field, and no icon, rows, or columns either — those come from the main config. So pages only contain `Apps=`. In the app listings here you can use your `folderN` names to get folders to show up.

App listings in both folders and pages are the same as string arrays in desktop files, i.e. no quotes, with entries separated by commas.

Any app discovered in your desktop file directories that hasn't been mentioned in the config ends up in a "misc page" at the very end of your pages. If you incorrectly reference a desktop file, it'll show up with a broken-icon placeholder (and of course won't launch). Referencing an undefined folder shows a blank/empty slot instead.

It should be noted that some apps come with org info attached to their desktop files, for example `org.freecad.FreeCAD`. This is needed to ID the file, as prefix stripping isn't implemented and might cause issues. So if you want to point to a desktop file like FreeCAD's, you need to write the full name in your Apps block.

## Disclaimer
As you might be able to tell, a big part of this project was made using claude-code. If you are opposed to LLM code do not use this launcher.

The initial desktop file and icon parsing logic using a state machine to be """blazingly fast""" was written by me well over a year ago. And Claude has not touched this code much at all. But effectively all the rendering code and UI/UX is claude. I could have written it myself, but I kinda just wanted the project done as I was getting annoyed with my previous launcher, and knowing my horribly slow coding speed AI felt easier, especially as I don't have much UI/UX experience nor patience to figure that out. This is also my first project using machine made code, so it was an interesting experience.

I have attached my current as of writing config file to the repo if you want a starting example.

build by running `make all`

feel free to make pull requests if you want to add something.
