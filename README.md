# Property Watcher
A **runtime variable watch window** for **Unreal Engine** using **ImGui**.

![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/2d26cc5b-a054-4200-ac5c-80d698e44d73.gif)

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
| ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/4501e1ec-4403-4af0-8d55-d361f5db7138?raw=true)         | ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/a34b5a53-1ca5-4071-81e6-1b2bf836259e?raw=true) | ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/8b4c138b-5cdd-4fb2-96bb-a264bf91ab2e?raw=true) |

| Actor search           | Watch window    | Inlining              |
|--|--|--|
| ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/6f999ec7-313f-46c4-b8f4-47112cb98ea6?raw=true)   | ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/2b03239c-f587-4912-8ab5-99ca1aa0680c?raw=true)  | ![](https://github.com/guitarfreak/PropertyWatcher/assets/1862206/791b9c37-6445-4470-89ed-fe2e5704dd4b?raw=true)     |
