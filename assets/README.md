# Artwork

## The menus' frame, by morpheus284

`menu_frame.png` is the stone frame of the in-game menus (the portal menu, F4 and the other panels). It is cut from
`card_base.png`, original artwork by [morpheus284](https://github.com/morpheus284) made for Core of Light's start
screen ([Core of Light #78](https://github.com/tsapZ/SkylandersCoreOfLight/issues/78)) and licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).

Changed for the menus: only the card's rim and its shadow are kept (the inside made transparent), and its bottom edge
is the top edge mirrored (the card's bottom carries an emblem). It is built into the games (CMakeLists.txt makes the
bytes `portal_menu.cpp` includes) and drawn as a 9-slice around each panel (`menu_draw.cpp`, DrawPanel).

## The PlayStation button prompts, by Laytonator2

`prompts_playstation/` holds the PlayStation buttons the games' prompts show with the PlayStation setting (cross,
circle, square, triangle, Start, Select, L1/R1, L2/R2 and the stick presses L/R). Original artwork by
[Laytonator2](https://github.com/Laytonator2), drawn in the PS3's style for these ports
([Core of Light #112](https://github.com/tsapZ/SkylandersCoreOfLight/issues/112)) and licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).

Changed: each picture cropped to what it shows and scaled from 2048 to 256 pixels on its longer side. They are built
into the games the same way (`prompt_art_png.inc`, included by `button_prompts.cpp`) and drawn in place of the icons
`prompt_icons.cpp` draws; the d-pad is still one of those.
