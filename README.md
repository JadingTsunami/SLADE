## SLADE  

A fork of slade where I keep my own personal QoL enhancement features, mostly for the Map editor:

* Y-Axis texture alignment
* Relative offsets for numerical fields in the context menu (e.g., select a bunch of items and ++/-- to increase or decrease by a given amount)
* Support mathematical expressions in numerical fields (e.g., 128-(2^4+128/4) becomes 80)
* Mouseless thing/texture browsing: Thing/Texture browser focus on filter field is automatic. Arrows work while focused on filter field. Enter applies selection.
* Keybind for changing line specials in 2D editor mode
* Keybind for creating doors
* Keybind for going to 3D mode at the current mouse location
* Keybinds for shape creation (ellipse vs. rectangle)
* Keybinds for shape features (number of sides, centered, locked ratio)
* Remember the last resource archives used when opening maps
* Option (with keybind) to move things when moving sectors
* Draw line lengths when selecting lines or moving/changing their size
* Draw sector special and/or tag when selecting
* Allow shift-snapping to existing draw points instead of just vertices
* Add MBF specials to Boom linedef definitions
* Add running increment and decrement options to properties panel.
* Hold "alt" and left-click in 3D mode to perform "Restricted" flood selection (only select same upper/mid/lower texture as the clicked one, and only the same line side (i.e., front vs. back))
* Add keybind (Ctrl+Shift+E) to toggle last selection
* Ability to "chop" an ellipse to create semicircles. Keybind for the same.
* Apply SHIFT key when pressed to quick texture seaches.
* Add color for sectors with "secret" special (not BOOM generalized sector compatible).
* When building sectors, automatically add upper/lower textures if they're needed, inheriting from nearby sectors.
* Keybind to automatically find and assign missing textures to walls.
* 2S lines that are impassable render with less transparency than non-blocking 2S lines.
* Respect "snap to grid" setting when splitting lines.
* Keybind to grow/shrink pasted map architecture before merging into the map.
* Keybind to rotate pasted map architecture before merging into the map.
* Start a sector property panel change with "F"/"C" for an expression starting from the floor/ceiling height, respectively. (Example: Setting a sector's ceiling height to "F+192" will calculate the ceiling height as 192 units above the sector floor height. Can be applied to multiple sectors at once.)
* Curve linedef operation with keybind.
* Bevel linedefs operation with keybind.
* 2D Mode: Alt-click to "smart select" lines connected to the current line. "Smart select" matches if 1S lines have matching midtextures, or if all textures match 2S lines.
