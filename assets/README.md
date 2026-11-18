# Artwork by morpheus284

`menu_frame.png` is the stone frame of the in-game menus (the portal menu, F4 and the other panels). It is cut from
`card_base.png`, original artwork by [morpheus284](https://github.com/morpheus284) made for Core of Light's start
screen ([Core of Light #78](https://github.com/tsapZ/SkylandersCoreOfLight/issues/78)) and licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).

Changed for the menus: only the card's rim and its shadow are kept (the inside made transparent), and its bottom edge
is the top edge mirrored (the card's bottom carries an emblem). It is built into the games (CMakeLists.txt makes the
bytes `portal_menu.cpp` includes) and drawn as a 9-slice around each panel (`menu_draw.cpp`, DrawPanel).
