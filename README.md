# Property Watcher
A **runtime variable watch window** for **Unreal Engine** using **ImGui**.

![Image](./Screenshots/demonstration.gif)

### Info

Uses the Unreal Engine metadata system to display class variables/functions like a debugger watch window.
You need to give it some initial objects/structs like gamestate/playercontroller/level and then you can branch out from there.

Can be usefull if you quickly want to check some data and you don't want to spend the time to write printfs or create other widgets to display information.

Also works in development builds.

Needs [**ImGui**](https://github.com/ocornut/imgui) to work, check Github for an Unreal ImGui plugin. \
For example there is [this](https://github.com/segross/UnrealImGui) or [this](https://github.com/benui-dev/UnrealImGui).

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
 - Copy/paste full subgraph via json serialisation.
 - Call functions via node connections.
 - Memory snapshot.
 - More variable manipulation, e.g.: add/remove/rearrange items in arrays.

### Gallery

| Handled types overview | Filtered search | Array inline          |
|--|--|--|
| ![](./Screenshots/types.png)         | ![](./Screenshots/search.png) | ![](./Screenshots/array_inline.png) |

| Actor search           | Watch window    | Inlining              |
|--|--|--|
| ![](./Screenshots/actorSearch.png)   | ![](./Screenshots/watch.png)  | ![](./Screenshots/inlining.png)     |
