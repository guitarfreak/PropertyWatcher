# Property Watcher
A **runtime variable watch window** for **Unreal Engine** using **ImGui**.

![Image](./Screenshots/demonstration.gif)

### Info

Uses the Unreal Engine metadata system to display class variables/functions like a debugger watch window.
You need to give it some initial objects/structs like gamestate/playercontroller/level and then you can branch out from there.

Can be usefull if you quickly want to check some data and you don't want to spend the time to write printfs or create other widgets to display information.

Also works in development builds.

### How to use

Copy **PropertyWatcher.h** and **PropertyWatcher.cpp** into your project source folder.
See **PropertyWatcher.h** for some **example usage code**.

Needs [**ImGui**](https://github.com/ocornut/imgui) to work, check Github for an Unreal ImGui backend plugin. \
For example, there is [this](https://github.com/segross/UnrealImGui) or [this](https://github.com/benui-dev/UnrealImGui).

### Features:
 - Manipulate primitive variables via ImGui widgets.
 - Watch window to remember variables.
 - Advanced search and filtering.
 - Subtree inlining.
 - Actors tab where you can display all actors or filter actors in a radius around the player.

### Future ideas:
 - Goto next search result.
 - Display value changes with colored animations in realtime.
 - Show actor component and widget hierarchy.
 - Custom draw functions for items.
 - Detachable tabs / multiple watch windows. (ImGui viewports?)
 - Copy/paste full subgraph via json serialization.
 - Call functions via node connections.
 - Memory snapshots.
 - More variable manipulation, e.g.: add/remove/rearrange items in arrays.

### Gallery

(Color theme is a modified version of **Blender Dark [Improvised]** from [ImguiCandy](https://github.com/Raais/ImguiCandy))

| Handled types overview | Filtered search | Array inline          |
|--|--|--|
| ![](./Screenshots/types.png?raw=true)         | ![](./Screenshots/search.png?raw=true) | ![](./Screenshots/array_inline.png?raw=true) |

| Actor search           | Watch window    | Inlining              |
|--|--|--|
| ![](./Screenshots/actorSearch.png?raw=true)   | ![](./Screenshots/watch.png?raw=true)  | ![](./Screenshots/inlining.png?raw=true)     |
