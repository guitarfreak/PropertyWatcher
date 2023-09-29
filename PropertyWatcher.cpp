/* 
	Development notes:

	Info:
		https://forums.unrealengine.com/t/a-sample-for-calling-a-ufunction-with-reflection/276416/9
		https://ikrima.dev/ue4guide/engine-programming/uobject-reflection/uobject-reflection/

		void UObject::ParseParms( const TCHAR* Parms )
		https://www.cnblogs.com/LynnVon/p/14794664.html

		https://github.com/ocornut/imgui/issues/718
		https://forums.unrealengine.com/t/using-clang-to-compile-on-windows/17478/51

	Bugs:
		- Classes left navigation doesnt work.
		- Blueprint functions have wrong parameters in property cpp type.
		- Delegates don't get inlined?
		- Crash on WeakPointerGet when bp selected camera search for private and disable classes.

		- Error when inlining this PlayerController.AcknowledgedPawn.PlayerState.PawnPrivate.Controller.PlayerInput.DebugExecBindings in watch tab
			and setting inline stack to 2
			Structs don't inline correctly in watch window as first item.
		- '/' can't be searched
		- Actor list should clear on restart because pointers will be invalid. Should stop using statics.

	ToDo:
		- Global settings to change float text input to sliders, and also global settings for drag change speed.
		- Fix blueprint struct members that have _C_0 and so on.
		- Button to clear custom storage like inlining.
		- Handle UberGaphFrame functions.
		- Column texts as value or string to distinguish between text and value filtering.
		- WorldOutline, Actor components, Widget tree.
		- Set favourites on variables, stored via classes/struct types, stored temporarily, should load.
			Should be put on top of list, maybe as "favourites" node.
		- Check performance.
		- Show the column keywords somewhere else and not in the helper popup.

		Easy to implement:
			- Ctrl + 123 to switch between tabs.
			- Make value was updated animations.
			- Make background transparent mode, overlay style.
			- If search input already focused, ctrl+f should select everything.

		Not Important:
			- Handle right clip copy and paste.
			- Export/import everything with json. (Try to use unreal serialization functions in properties.)
			- Watch items can be selected and deleted with del.
			- Selection happens on first cell background?
			- Sorting.
			- Better actors line trace.
			- Make save and restore functions for window settings.
			- Super bonus: Hide things that have changed from base object like in details viewer UE.
			- Ability to modify containers like array and map, add/remove/insert.
			- Look at EFieldIterationFlags
			- Metadata: Use keys: Category, UIMin, UIMax. (Maybe: tooltip, sliderexponent?)
			- Metadata categories: Property->MetaDataMap.
			- Add hot keys for enabling filter, classes, functions.
			- Call functions in watchItemPaths like Manager.SubObject.CallFunc:GetCurrentThing.Member.
			- Make it easier to add unique struct handlings from user side.
			- Do a clang compile test.

		Add before release:
			- Search should be able to do structvar.membervar = 5.
			- Better search: maybe make a mode "hide non value types" that hides a list of things like delegates that you never want to see.
				  Maybe 2 search boxes, second for permanent stuff like hiding delegates, first one for quick searches.

			- Search jump to next result.
			- Detached watch windows.
			- Custom draw functions for data.
				- Add right click "draw as" (rect, whatever).
				- Manipulator widget based on data like position, show in world.
			- Simple function calling.
				Make functions expandable in tree form, and leafs are arguments and return value.
			- Simple delegate calling.
			- Fix maps with touples.
			- Auto complete for watch window members.
			- Property path by member value.
				- Make path with custom getter-> instead of array index look at object and test for name or something.
				- Make custom path by dragging from a value, that should make link to parent node with that value being looked for.

			- Try imgui viewports.

	Ideas:
		- Print container preview up to property value region size, the more you open the column the more you see.
		- Make mode that diffs current values of object with default class variables.
		- To call functions dynamically we could use all our widgets for input and then store the result in an input.
			We could then display it with drawItem, all the information is in the properties of the function, including the result property.
		- Add mode to clamp property name text to right side and clip left so right side of text is always visible, which is more important.
		- Class pointer members maybe do default object.
		- Ability to make memory snapshot.

	Session:
		- Actor component tree.
		- Widget component tree.
		- Favourites.
		- Variable/function categories.
		- Add filter mode that hides everything.
*/

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
#endif

#if !UE_SERVER

#define PROPERTY_WATCHER_INTERNAL
#include "PropertyWatcher.h"

#include "imgui_internal.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Engine/StaticMeshActor.h"

#include "GameFramework/PlayerController.h"

#include "UObject/Stack.h"
#include "Engine/Level.h"

#include "GameFramework/Actor.h"

#include "Misc/ExpressionParser.h"
#include "Internationalization/Regex.h"

#include "Misc/TextFilterUtils.h"
#include <inttypes.h> // For printing address.

// Switch on to turn on unreal insights events for performance tests.
#define SCOPE_EVENT(name) 
//#define SCOPE_EVENT(name) SCOPED_NAMED_EVENT_TEXT(name, FColor::Orange);

#if WITH_EDITORONLY_DATA
#define MetaData_Available true
#else 
#define MetaData_Available false
#endif

namespace PropertyWatcher {

void ObjectsTab(bool DrawControls, TArray<PropertyItemCategory>& CategoryItems, TreeState* State = 0);
void ActorsTab(bool DrawControls, UWorld* World, TreeState* State = 0, ColumnInfos* ColInfos = 0, bool Init = false);
void WatchTab(bool DrawControls, TArray<MemberPath>&WatchedMembers, bool* WantsToSave, bool* WantsToLoad, TArray<PropertyItemCategory>&CategoryItems, TreeState * State = 0);

void Update(FString WindowName, TArray<PropertyItemCategory>& CategoryItems, TArray<MemberPath>& WatchedMembers, UWorld* World, bool* IsOpen, bool* WantsToSave, bool* WantsToLoad, bool Init) {
	SCOPE_EVENT("PropertyWatcher::Update");

	TMem.Init(TMemoryStartSize);
	defer{ TMem.ClearAll(); };

	*WantsToLoad = false;
	*WantsToSave = false;

	// Performance test.
	static const int TimerCount = 30;
	static double Timers[TimerCount] = {};
	static int TimerIndex = 0;
	double StartTime = FPlatformTime::Seconds();

	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	bool WindowIsOpen = ImGui::Begin(ImGui_StoA(*("Property Watcher: " + WindowName)), IsOpen, ImGuiWindowFlags_MenuBar); defer{ ImGui::End(); };
	if (!WindowIsOpen)
		return;

	static ImVec2 FramePadding = ImVec2(ImGui::GetStyle().CellPadding.x, 2);
	static bool ShowObjectNamesOnAllProperties = true;
	static bool ShowPerformanceInfo = false;

	// Menu.
	if (ImGui::BeginMenuBar()) {
		defer{ ImGui::EndMenuBar(); };
		if (ImGui::BeginMenu("Settings")) {
			defer{ ImGui::EndMenu(); };

			ImGui::SetNextItemWidth(150);
			ImGui::DragFloat2("Item Padding", &FramePadding[0], 0.1f, 0.0, 20.0f, "%.0f");
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				FramePadding = ImVec2(ImGui::GetStyle().FramePadding.x, 2);

			ImGui::Checkbox("Show object names on all properties", &ShowObjectNamesOnAllProperties);
			ImGuiAddon::QuickTooltip("This puts the (<ObjectName>) at the end of properties that are also objects.");

			ImGui::Checkbox("Show debug/performance info", &ShowPerformanceInfo);
			ImGuiAddon::QuickTooltip("Displays item count and average elapsed time in ms.");
		}
		if (ImGui::BeginMenu("Help")) {
			defer{ ImGui::EndMenu(); };
			ImGui::Text(HelpText);
		}
	}

	ColumnInfos ColInfos = {};
	{
		int FlagNoSort = ImGuiTableColumnFlags_NoSort;
		int FlagDefault = ImGuiTableColumnFlags_WidthStretch;
		ColInfos.Infos.Add({ ColumnID_Name,     "name",     "Property Name",  FlagDefault | ImGuiTableColumnFlags_NoHide });
		ColInfos.Infos.Add({ ColumnID_Value,    "value",    "Property Value", FlagDefault | FlagNoSort });
		ColInfos.Infos.Add({ ColumnID_Metadata, "metadata", "Metadata",       FlagNoSort | ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("(?)").x });
		ColInfos.Infos.Add({ ColumnID_Type,     "type",     "Property Type",  FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide });
		ColInfos.Infos.Add({ ColumnID_Cpptype,  "cpptype",  "CPP Type",       FlagDefault });
		ColInfos.Infos.Add({ ColumnID_Class,    "class",    "Owner Class",    FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide });
		ColInfos.Infos.Add({ ColumnID_Category, "category", "Category",       FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide });
		ColInfos.Infos.Add({ ColumnID_Address,  "address",  "Adress",         FlagDefault | ImGuiTableColumnFlags_DefaultHide });
		ColInfos.Infos.Add({ ColumnID_Size,     "size",     "Size",           FlagDefault | ImGuiTableColumnFlags_DefaultHide });
		ColInfos.Infos.Add({ ColumnID_Remove,   "",         "Remove",         ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() });
	}

	// Top region.
	static bool ListFunctionsOnObjectItems = false;
	static bool EnableClassCategoriesOnObjectItems = true;
	static bool SearchFilterActive = false;
	static char SearchString[100];
	SimpleSearchParser SearchParser;
	{
		// Search.
		bool SelectAll = false;
		if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_F)) {
			SelectAll = true;
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 280);
		int Flags = ImGuiInputTextFlags_AutoSelectAll;
		ImGui::InputTextWithHint("##SearchEdit", "Search Properties (Ctrl+F)", SearchString, IM_ARRAYSIZE(SearchString), Flags);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip(); defer{ ImGui::EndTooltip(); };

			FString Text = SearchBoxHelpText;
			Text.Append("\nCurrent keywords for columns are: \n\t");
			Text.Append(FString::Join(ColInfos.GetSearchNameArray(), *FString(", ")));
			ImGui::Text(ImGui_StoA(*Text));
		}
		ImGui::SameLine();
		//ImGuiWindow* window = ImGui::GetCurrentWindow();
		auto Window = ImGui::GetCurrentContext()->CurrentWindow;
		ImGui::Checkbox("Filter", &SearchFilterActive);
		ImGuiAddon::QuickTooltip("Enable filtering of rows that didn't pass the search in the search box.");
		ImGui::SameLine();
		ImGui::Checkbox("Classes", &EnableClassCategoriesOnObjectItems);
		ImGuiAddon::QuickTooltip("Enable sorting of actor member variables by classes with subsections.");
		ImGui::SameLine();
		ImGui::Checkbox("Functions", &ListFunctionsOnObjectItems);
		ImGuiAddon::QuickTooltip("Show functions in actor items.");
		ImGui::Spacing();

		SearchParser.ParseExpression(SearchString, ColInfos.GetSearchNameArray());
	}

	// Tabs.
	int ItemCount = 0;
	float ScrollRegionHeight = 0;
	if (ImGui::BeginTabBar("MyTabBar")) {
		defer{ ImGui::EndTabBar(); };

		bool AddressHoveredThisFrame = false;
		static void* HoveredAddress = 0;
		static bool DrawHoveredAddresses = false;
		defer{ DrawHoveredAddresses = AddressHoveredThisFrame; };

		static TArray<FString> Tabs = { "Objects", "Actors", "Watch" };
		for (auto CurrentTab : Tabs) {
			if (ImGui::BeginTabItem(ImGui_StoA(*CurrentTab))) {
				defer{ ImGui::EndTabItem(); };

				if (CurrentTab == "Watch")
					WatchTab(true, WatchedMembers, WantsToSave, WantsToLoad, CategoryItems);
				else if (CurrentTab == "Actors")
					ActorsTab(true, World);

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, FramePadding); defer{ ImGui::PopStyleVar(); };

				ImVec2 TableSize = ImVec2(0, ImGui::GetContentRegionAvail().y);
				if (ShowPerformanceInfo)
					TableSize.y -= ImGui::GetTextLineHeightWithSpacing();

				ImGuiTableFlags TableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SortTristate;
				if (CurrentTab == "Actors")
					TableFlags |= ImGuiTableFlags_Sortable;

				// Table.
				if (ImGui::BeginTable("table", ColInfos.Infos.Num(), TableFlags, TableSize)) {
					ImGui::TableSetupScrollFreeze(0, 1);
					for (int i = 0; i < ColInfos.Infos.Num(); i++) {
						auto It = ColInfos.Infos[i];
						int Flags = It.Flags;
						if (It.DisplayName == "Remove" && CurrentTab != "Watch")
							Flags |= ImGuiTableColumnFlags_Disabled;
						ImGui::TableSetupColumn(ImGui_StoA(*It.DisplayName), Flags, It.InitWidth, i);
					}
					ImGui::TableHeadersRow();
					
					TArray<FString> CurrentPath;
					TreeState State = {};
					State.SearchFilterActive = SearchFilterActive;
					State.DrawHoveredAddress = DrawHoveredAddresses;
					State.HoveredAddress = HoveredAddress;
					State.CurrentWatchItemIndex = -1;
					State.EnableClassCategoriesOnObjectItems = EnableClassCategoriesOnObjectItems;
					State.ListFunctionsOnObjectItems = ListFunctionsOnObjectItems;
					State.ShowObjectNamesOnAllProperties = ShowObjectNamesOnAllProperties;
					State.SearchParser = SearchParser;
					State.ScrollRegionRange = FFloatInterval(ImGui::GetScrollY(), ImGui::GetScrollY() + TableSize.y);
					
					defer {
						if (State.AddressWasHovered) {
							State.AddressWasHovered = false;
							AddressHoveredThisFrame = true;
							HoveredAddress = State.HoveredAddress;
						};
					};

					if (CurrentTab == "Objects")
						ObjectsTab(false, CategoryItems, &State);
					else if (CurrentTab == "Actors")
						ActorsTab(false, World, &State, &ColInfos, Init);
					else if (CurrentTab == "Watch")
						WatchTab(false, WatchedMembers, WantsToSave, WantsToLoad, CategoryItems, &State);

					// Allow overscroll.
					{
						// Calculations are off, but it's fine for now.
						float Offset = ImGui::TableGetHeaderRowHeight() + ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().FramePadding.y * 2;
						ImGui::TableNextRow(0, TableSize.y - Offset);
						ImGui::TableNextColumn();

						auto BGColor = ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg);
						ImGui::TableSetBgColor(ImGuiTableBgTarget_::ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(BGColor), -1);
					}

					ItemCount = State.ItemDrawCount;
					ScrollRegionHeight = ImGui::GetScrollMaxY();
					ImGui::EndTable();
				}
			}
		}
	}

	if (ShowPerformanceInfo) {
		ImGui::Text("Item: %d", ItemCount);
		ImGui::SameLine();
		ImGui::Text("-");
		ImGui::SameLine();
		{
			Timers[TimerIndex] = FPlatformTime::Seconds() - StartTime;
			TimerIndex = (TimerIndex + 1) % TimerCount;
			double AverageTime = 0;
			for (int i = 0; i < TimerCount; i++)
				AverageTime += Timers[i];
			AverageTime /= TimerCount;
			ImGui::Text("%.3f ms", AverageTime * 1000.0f);
		}
		ImGui::SameLine();
		ImGui::Text("-");
		ImGui::SameLine();
		ImGui::Text("ScrollHeight: %.0f", ScrollRegionHeight);

		ImGui::SameLine();
		ImGui::Text("-");
		ImGui::SameLine();

		ImGui::Text("HoveredId: %u", ImGui::GetHoveredID());
	}

	ImRect TargetRect(ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax());
	TargetRect.Translate(ImGui::GetWindowPos());
	TargetRect.Translate(ImVec2(0, ImGui::GetScrollY()));

	int HoveredID = ImGui::GetHoveredID() == 0 ? 1 : ImGui::GetHoveredID();
	if (ImGui::BeginDragDropTargetCustom(TargetRect, HoveredID)) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PropertyWatcherMember")) {
			char* Payload = (char*)payload->Data;

			MemberPath Member;
			Member.PathString = Payload;
			WatchedMembers.Push(Member);
		}
	}
}

void ObjectsTab(bool DrawControls, TArray<PropertyItemCategory>& CategoryItems, TreeState* State) {
	if (DrawControls) {
		return;
	}
	
	TInlineComponentArray<FAView> CurrentPath;
	for (auto& Category : CategoryItems) {
		bool MakeCategorySection = !Category.Name.IsEmpty();

		TreeNodeState NodeState = {};
		if (MakeCategorySection)
			BeginSection(TMem.SToA(Category.Name), NodeState, *State, -1, ImGuiTreeNodeFlags_DefaultOpen);

		if (NodeState.IsOpen || !MakeCategorySection)
			for (auto& Item : Category.Items)
				DrawItemRow(*State, Item, CurrentPath);

		if (MakeCategorySection)
			EndSection(NodeState, *State);
	}
}

void ActorsTab(bool DrawControls, UWorld* World, TreeState* State, ColumnInfos* ColInfos, bool Init) {
	static TArray<PropertyItem> ActorItems;
	static bool UpdateActorsEveryFrame = false;
	static bool SearchAroundPlayer = false;
	static float ActorsSearchRadius = 5;
	static bool DrawOverlapSphere = false;

	if (Init)
		ActorItems.Empty();

	if (DrawControls) {
		static TArray<bool> CollisionChannelsActive;
		static bool InitActors = true;
		if (InitActors) {
			InitActors = false;
			int NumCollisionChannels = StaticEnum<ECollisionChannel>()->NumEnums();
			for (int i = 0; i < NumCollisionChannels; i++)
				CollisionChannelsActive.Push(false);
		}

		if (!World)
			return;

		if (World->GetCurrentLevel()) {
			ImGui::Text("Current World: %s, ", ImGui_StoA(*World->GetName()));
			ImGui::SameLine();
			ImGui::Text("Current Level: %s", ImGui_StoA(*World->GetCurrentLevel()->GetName()));
			ImGui::Spacing();
		}

		bool UpdateActors = UpdateActorsEveryFrame;
		if (UpdateActorsEveryFrame) ImGui::BeginDisabled();
		if (ImGui::Button("Update Actors"))
			UpdateActors = true;
		ImGui::SameLine();
		if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0)))
			ActorItems.Empty();
		if (UpdateActorsEveryFrame) ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::Checkbox("Update actors every frame", &UpdateActorsEveryFrame);
		ImGui::SameLine();
		ImGui::Checkbox("Search around player", &SearchAroundPlayer);
		ImGui::Spacing();

		bool DoRaytrace = false;
		{
			if (!SearchAroundPlayer) ImGui::BeginDisabled();

			if (ImGui::Button("Set Channels"))
				ImGui::OpenPopup("SetChannelsPopup");

			if (ImGui::BeginPopup("SetChannelsPopup")) {
				for (int i = 0; i < CollisionChannelsActive.Num(); i++) {
					FString Name = StaticEnum<ECollisionChannel>()->GetNameStringByIndex(i);
					ImGui::Selectable(ImGui_StoA(*Name), &CollisionChannelsActive[i], ImGuiSelectableFlags_DontClosePopups);
				}

				ImGui::Separator();
				if (ImGui::Button("Clear All"))
					for (auto& It : CollisionChannelsActive)
						It = false;

				ImGui::EndPopup();
			}

			static bool RaytraceReady = false;
			ImGui::SameLine();
			if (ImGui::Button("Do Mouse Trace")) {
				ImGui::OpenPopup("PopupMouseTrace");
				RaytraceReady = true;
			}

			if (ImGui::BeginPopup("PopupMouseTrace")) {
				ImGui::Text("Click on screen to trace object.");
				ImGui::EndPopup();
			} else {
				if (RaytraceReady) {
					RaytraceReady = false;
					DoRaytrace = true;
				}
			}

			ImGui::SetNextItemWidth(150);
			ImGui::InputFloat("Search radius in meters", &ActorsSearchRadius, 1.0, 1.0, "%.1f");
			ImGui::SameLine();
			ImGui::Checkbox("Draw Search sphere", &DrawOverlapSphere);

			if (!SearchAroundPlayer) ImGui::EndDisabled();
		}

		{
			APlayerController* PlayerController = World->GetFirstPlayerController();
			TArray<TEnumAsByte<EObjectTypeQuery>> traceObjectTypes;
			for (int i = 0; i < CollisionChannelsActive.Num(); i++) {
				bool Active = CollisionChannelsActive[i];
				if (Active)
					traceObjectTypes.Add(UEngineTypes::ConvertToObjectType((ECollisionChannel)i));
			}

			FVector SpherePos = PlayerController->GetPawn()->GetActorTransform().GetLocation();
			if (UpdateActors) {
				ActorItems.Empty();

				if (SearchAroundPlayer) {
					TArray<AActor*> ResultActors;
					{
						//UClass* seekClass = AStaticMeshActor::StaticClass();					
						UClass* seekClass = 0;
						TArray<AActor*> ignoreActors = {};

						UKismetSystemLibrary::SphereOverlapActors(World, SpherePos, ActorsSearchRadius * 100, traceObjectTypes, seekClass, ignoreActors, ResultActors);
					}

					for (auto It : ResultActors) {
						if (!It) continue;
						ActorItems.Push(MakeObjectItem(It));
					}

				} else {
					ULevel* CurrentLevel = World->GetCurrentLevel();
					if (CurrentLevel) {
						for (auto& Actor : CurrentLevel->Actors) {
							if (!Actor) continue;
							ActorItems.Push(MakeObjectItem(Actor));
						}
					}
				}
			}

			if (DrawOverlapSphere)
				DrawDebugSphere(World, SpherePos, ActorsSearchRadius * 100, 20, FColor::Purple, false, 0.0f);

			if (DoRaytrace) {
				FHitResult HitResult;
				bool Result = PlayerController->GetHitResultUnderCursorForObjects(traceObjectTypes, true, HitResult);
				if (Result) {
					AActor* HitActor = HitResult.GetActor();
					if (HitActor)
						ActorItems.Push(MakeObjectItem(HitActor));
				}
			}
		}

		return;
	}

	// Sorting.
	{
		static ImGuiTableSortSpecs* s_current_sort_specs = NULL;
		auto SortFun = [](const void* lhs, const void* rhs) -> int {
			PropertyItem* a = (PropertyItem*)lhs;
			PropertyItem* b = (PropertyItem*)rhs;
			for (int n = 0; n < s_current_sort_specs->SpecsCount; n++) {
				const ImGuiTableColumnSortSpecs* sort_spec = &s_current_sort_specs->Specs[n];
				int delta = 0;
				switch (sort_spec->ColumnUserID) {
					//case ColumnID_Name:    delta = (strcmp(ImGui_StoA(*((UObject*)a->Ptr)->GetName()), ImGui_StoA(*((UObject*)b->Ptr)->GetName())));
					//case ColumnID_Cpptype: delta = strcmp(a->GetCPPType().GetData(), b->GetCPPType().GetData());
					case ColumnID_Name: {
						delta = FCString::Strcmp(*((UObject*)a->Ptr)->GetName(), *((UObject*)b->Ptr)->GetName());
					} break;
					case ColumnID_Cpptype: {
						delta = a->GetCPPType().Compare(b->GetCPPType());
					} break;
					case ColumnID_Address: {
						delta = (int64)a->Ptr - (int64)b->Ptr;
					} break;
					case ColumnID_Size: {
						delta = a->GetSize() - b->GetSize();
					} break;
					default: IM_ASSERT(0);
				}
				if (delta > 0)
					return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? +1 : -1;
				if (delta < 0)
					return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? -1 : +1;
			}

			return ((int64)a->Ptr - (int64)b->Ptr);
		};

		if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs()) {
			if (sorts_specs->SpecsDirty) {
				s_current_sort_specs = sorts_specs; // Store in variable accessible by the sort function.
				if (ActorItems.Num() > 1)
					qsort(ActorItems.GetData(), (size_t)ActorItems.Num(), sizeof(ActorItems[0]), SortFun);
				s_current_sort_specs = NULL;
				sorts_specs->SpecsDirty = false;
			}
		}
	}
	
	TInlineComponentArray<FAView> CurrentPath;
	for (auto& Item : ActorItems)
		DrawItemRow(*State, Item, CurrentPath);
}

void WatchTab(bool DrawControls, TArray<MemberPath>& WatchedMembers, bool* WantsToSave, bool* WantsToLoad, TArray<PropertyItemCategory>& CategoryItems, TreeState* State) {
	if (DrawControls) {
		if (ImGui::Button("Clear All"))
			WatchedMembers.Empty();
		ImGui::SameLine();
		if (ImGui::Button("Save"))
			*WantsToSave = true;
		ImGui::SameLine();
		if (ImGui::Button("Load"))
			*WantsToLoad = true;

		return;
	}

	TArray<PropertyItem> Items;
	for (auto& It : CategoryItems)
		Items.Append(It.Items);

	TInlineComponentArray<FAView> CurrentPath;
	int MemberIndexToDelete = -1;
	bool MoveHappened = false;
	int MoveIndexFrom = -1;
	int MoveIndexTo = -1;
	FString NewPathName;

	int i = 0;
	for (auto& Member : WatchedMembers) {
		State->CurrentWatchItemIndex = i++;
		State->WatchItemGotDeleted = false;
		State->RenameHappened = false;
		State->PathStringPtr = &Member.PathString;

		bool Found = Member.UpdateItemFromPath(Items);
		if (!Found) {
			Member.CachedItem.Ptr = 0;
			Member.CachedItem.Prop = 0;
		}

		DrawItemRow(*State, Member.CachedItem, CurrentPath);

		if (State->WatchItemGotDeleted)
			MemberIndexToDelete = State->CurrentWatchItemIndex;

		if (State->MoveHappened) {
			MoveHappened = true;
			MoveIndexFrom = State->MoveFrom;
			MoveIndexTo = State->MoveTo;
		}
	}

	if (MemberIndexToDelete != -1)
		WatchedMembers.RemoveAt(MemberIndexToDelete);

	if (MoveHappened)
		WatchedMembers.Swap(MoveIndexFrom, MoveIndexTo);
}

//

void TreeState::EnableForceToggleNode(bool Mode, int StackIndexLimit) {
	ForceToggleNodeOpenClose = true;
	ForceToggleNodeMode = Mode;
	ForceToggleNodeStackIndexLimit = StackIndexLimit;

	VisitedPropertiesStack.Empty();
}

bool TreeState::ItemIsInfiniteLooping(VisitedPropertyInfo& PropertyInfo) {
	if (!PropertyInfo.Address)
		return false;

	int Count = 0;
	for (auto& It : VisitedPropertiesStack) {
		if (It.Compare(PropertyInfo)) {
			Count++;
			if (Count == 3) // @Todo: Think about what's appropriate.
				return true;
		}
	}
	return false;
}

//

void DrawItemRow(TreeState& State, PropertyItem& Item, TInlineComponentArray<FAView>& CurrentMemberPath, int StackIndex) {
	SCOPE_EVENT("PropertyWatcher::DrawItemRow");

	if (State.ItemDrawCount > 100000) // @Todo: Random safety measure against infinite recursion, could be better?
		return;

	bool ItemCanBeOpened = Item.CanBeOpened();
	bool ItemIsVisible = State.IsCurrentItemVisible();
	bool SearchIsActive = (bool)State.SearchParser.Commands.Num();

	CachedColumnText ColumnTexts;
	FAView ItemDisplayName;
	bool ItemIsSearched = false;
	
	if (!ItemIsVisible && !State.SearchFilterActive) {
		ItemDisplayName = "";

	} else {
		ItemDisplayName = GetColumnCellText(Item, ColumnID_Name, &State, &CurrentMemberPath, &StackIndex);

		if (SearchIsActive) {
			ColumnTexts.Add(ColumnID_Name, ItemDisplayName); // Default.

			// Cache the cell texts that we need for the text search.
			for (auto& Command : State.SearchParser.Commands)
				if (Command.Type == SimpleSearchParser::Command_Test && !ColumnTexts.Get(Command.Tst.ColumnID)) {
					FAView view = GetColumnCellText(Item, Command.Tst.ColumnID, &State, &CurrentMemberPath, &StackIndex);
					if (view.IsEmpty()) {
						int stop = 234;
					}
					ColumnTexts.Add(Command.Tst.ColumnID, view);
				}

			ItemIsSearched = State.SearchParser.ApplyTests(ColumnTexts);
		}
	}

	auto FindOrGetColumnText = [&Item, &ColumnTexts](int ColumnID) -> FAView {
		if (FAView* Result = ColumnTexts.Get(ColumnID))
			return *Result;
		else
			return GetColumnCellText(Item, ColumnID);
	};

	// Item is skipped.
	if (State.SearchFilterActive && !ItemIsSearched && !ItemCanBeOpened)
		return;

	// Misc setup.
	
	State.ItemDrawCount++;
	FAView ItemAuthoredName = ItemCanBeOpened ? Item.GetAuthoredName() : "";
	if (ItemCanBeOpened)
		CurrentMemberPath.Push(ItemAuthoredName);

	TreeNodeState NodeState;
	{
		NodeState = {};
		NodeState.HasBranches = ItemCanBeOpened;
		NodeState.ItemInfo.Set(Item);

		if (ItemIsVisible && ItemIsSearched) {
			NodeState.PushTextColor = true;
			NodeState.TextColor = ImVec4(1, 0.5f, 0, 1);
		}
	}

	bool IsTopWatchItem = State.CurrentWatchItemIndex != -1 && StackIndex == 0;
	if (IsTopWatchItem)
		ImGui::PushID(State.CurrentWatchItemIndex);

	// @Column(name): Property name
	{
		if (ItemIsVisible && !Item.IsValid())
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		BeginTreeNode(*ItemAuthoredName, *ItemDisplayName, NodeState, State, StackIndex, 0);

		bool NodeIsMarkedAsInlined = false;

		// Right click popup for inlining.
		if(NodeState.HasBranches) {
			// Do tree push to get right bool from storage when node is open or closed.
			if (!NodeState.IsOpen) 
				ImGui::TreePush(*ItemAuthoredName);

			ImGuiID StorageIDIsInlined = ImGui::GetID("IsInlined");
			ImGuiID StorageIDInlinedStackDepth = ImGui::GetID("InlinedStackDepth");

			auto Storage = ImGui::GetStateStorage();
			NodeIsMarkedAsInlined = Storage->GetBool(StorageIDIsInlined);
			int InlinedStackDepth = Storage->GetInt(StorageIDInlinedStackDepth, 1);

			if (NodeIsMarkedAsInlined && !State.ForceInlineChildItems && InlinedStackDepth)
				TreeNodeSetInline(NodeState, State, CurrentMemberPath.Num(), StackIndex, InlinedStackDepth);

			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("ItemPopup");

			if (ImGui::BeginPopup("ItemPopup")) {
				if (ImGui::Checkbox("Inlined", &NodeIsMarkedAsInlined))
					Storage->SetBool(StorageIDIsInlined, NodeIsMarkedAsInlined);

				ImGui::SameLine();
				ImGui::BeginDisabled(!NodeIsMarkedAsInlined);
				if (ImGui::SliderInt("Stack Depth", &InlinedStackDepth, 0, 9))
					Storage->SetInt(StorageIDInlinedStackDepth, InlinedStackDepth);
				ImGui::EndDisabled();

				ImGui::EndPopup();
			}

			if (!NodeState.IsOpen) 
				ImGui::TreePop();
		}

		if (ItemIsVisible)
		{
			// Drag to watch window.
			if (StackIndex > 0 && Item.Type != PointerType::Function) {
				if (ImGui::BeginDragDropSource()) {
					TMemBuilder(Builder);
					for (int i = 0; i < CurrentMemberPath.Num(); i++) {
						if (i > 0) Builder.AppendChar('.');
						Builder.Append(CurrentMemberPath[i]);
					}
					if (!NodeState.HasBranches)
						Builder << '.' << ItemDisplayName;

					ImGui::SetDragDropPayload("PropertyWatcherMember", *Builder, Builder.Len());
					ImGui::Text("Add to watch list:");
					ImGui::Text(*Builder);
					ImGui::EndDragDropSource();
				}
			}

			// Drag to move watch item. Only available when top level watch list item.
			if (IsTopWatchItem) {
				if (ImGui::BeginDragDropSource()) {
					ImGui::SetDragDropPayload("PropertyWatcherMoveIndex", &State.CurrentWatchItemIndex, sizeof(int));
					ImGui::Text("Move Item: %d", State.CurrentWatchItemIndex);
					ImGui::EndDragDropSource();
				}

				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PropertyWatcherMoveIndex")) {
						if (Payload->Data) {
							State.MoveHappened = true;
							State.MoveFrom = *(int*)Payload->Data;
							State.MoveTo = State.CurrentWatchItemIndex;
						}
					}
					ImGui::EndDragDropTarget();
				}

				// Handle watch list item path editing.
				if (State.PathStringPtr) {
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());

					FString StringID = FString::Printf(TEXT("##InputPathText %d"), State.CurrentWatchItemIndex);

					static TArray<char> StringBuffer;
					StringBuffer.Empty();
					StringBuffer.Append(ImGui_StoA(**State.PathStringPtr), State.PathStringPtr->Len() + 1);
					if (ImGuiAddon::InputText(ImGui_StoA(*StringID), StringBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
						(*State.PathStringPtr) = FString(StringBuffer);

					ImGui::PopStyleColor(1);
				}
			}

			if (NodeIsMarkedAsInlined) {
				ImGui::SameLine();
				ImGui::Text("*");
			}

			// This puts the (<ObjectName>) at the end of properties that are also objects.
			if (State.ShowObjectNamesOnAllProperties) {
				if (Item.Type == PointerType::Property && CastField<FObjectProperty>(Item.Prop) && Item.Ptr) {
					ImGui::SameLine();
					ImGui::BeginDisabled();
					FName Name = ((UObject*)Item.Ptr)->GetFName();
					ImGui::Text(*TMem.Printf("(%s)", *TMem.NToA(Name)));
					ImGui::EndDisabled();
				}
			}
		}

		if (ItemIsVisible && !Item.IsValid())
			ImGui::PopStyleColor(1);
	}

	// Draw other columns if visible. 
	if (!ItemIsVisible || NodeState.ItemIsInlined) {
		ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);

	} else {
		// @Column(value): Property Value
		if (ImGui::TableNextColumn()) {
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (Item.IsValid())
				DrawPropertyValue(Item);
		}

		// @Column(metadata): Metadata							
		if (ImGui::TableNextColumn() && Item.Prop) {
			if (ItemHasMetaData(Item)) {
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					
					if (MetaData_Available)
						ImGui::TextUnformatted(*FindOrGetColumnText(ColumnID_Metadata));
					else
						ImGui::TextUnformatted("Data not available in shipping builds.");

					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::IsItemHovered();
			}
		}

		// @Column(type): Property Type
		if (ImGui::TableNextColumn()) {
			if (Item.IsValid()) {
				{
					ImVec4 cText = ImVec4(0, 0, 0, 0);
					GetItemColor(Item, cText);
					ImGui::PushStyleColor(ImGuiCol_Text, cText); 
					ImGui::Bullet();
					ImGui::PopStyleColor();
				}
				ImGui::SameLine();
				ImGui::Text(*FindOrGetColumnText(ColumnID_Type));
			}
		}

		// @Column(cpptype): CPP Type
		if (ImGui::TableNextColumn())
			ImGui::Text(*FindOrGetColumnText(ColumnID_Cpptype));

		// @Column(class): Owner Class
		if (ImGui::TableNextColumn())
			ImGui::Text(*FindOrGetColumnText(ColumnID_Class));

		// @Column(category): Metadata Category
		if (ImGui::TableNextColumn())
			ImGui::Text(*FindOrGetColumnText(ColumnID_Category));

		// @Column(address): Adress
		if (ImGui::TableNextColumn()) {
			if (State.DrawHoveredAddress && Item.Ptr == State.HoveredAddress)
				ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), *FindOrGetColumnText(ColumnID_Address));
			else
				ImGui::Text(*FindOrGetColumnText(ColumnID_Address));

			if (ImGui::IsItemHovered()) {
				State.AddressWasHovered = true;
				State.HoveredAddress = Item.Ptr;
			}
		}

		// @Column(size): Size
		if (ImGui::TableNextColumn())
			ImGui::Text(*FindOrGetColumnText(ColumnID_Size));

		// Close Button
		if (ImGui::TableNextColumn())
			if (IsTopWatchItem)
				if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0)))
					State.WatchItemGotDeleted = true;
	}

	// Draw leaf properties.
	if (NodeState.IsOpen) {
		bool PushAddressesStack = State.ForceToggleNodeOpenClose || State.ForceInlineChildItems;
		if (PushAddressesStack)
			State.VisitedPropertiesStack.Push(NodeState.ItemInfo);
		TMem.PushMarker();

		DrawItemChildren(State, Item, CurrentMemberPath, StackIndex);
		
		TMem.PopMarker();
		if (PushAddressesStack)
			State.VisitedPropertiesStack.Pop(false);
	}

	if (ItemCanBeOpened)
		CurrentMemberPath.Pop(false);

	EndTreeNode(NodeState, State);

	if (IsTopWatchItem)
		ImGui::PopID();
}

void DrawItemChildren(TreeState& State, PropertyItem& Item, TInlineComponentArray<FAView>& CurrentMemberPath, int StackIndex) {
	check(Item.Ptr); // Do we need this check here? Can't remember.

	if (Item.Prop &&
		(Item.Prop->IsA(FWeakObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FLazyObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FSoftObjectProperty::StaticClass()))) {
		UObject* Obj = 0;
		bool IsValid = GetObjFromObjPointerProp(Item, Obj);
		if (IsValid) {
			return DrawItemChildren(State, MakeObjectItem(Obj), CurrentMemberPath, StackIndex + 1);
		}
	}

	bool ItemIsObjectProp = Item.Prop && Item.Prop->IsA(FObjectProperty::StaticClass());
	bool ItemIsObject = ItemIsObjectProp || Item.Type == PointerType::Object;

	// Members.
	{
		TArray<PropertyItem> Members;
		Item.GetMembers(&Members);

		SectionHelper SectionHelper;
		if (State.EnableClassCategoriesOnObjectItems && ItemIsObject) {
			for (auto& Member : Members)
				SectionHelper.Add(((FField*)(Member).Prop)->Owner.GetFName());

			SectionHelper.Init();
		}

		if (!SectionHelper.Enabled) {
			for (auto It : Members)
				DrawItemRow(State, It, CurrentMemberPath, StackIndex + 1);

		} else {
			for (int SectionIndex = 0; SectionIndex < SectionHelper.GetSectionCount(); SectionIndex++) {
				int MemberStartIndex, MemberEndIndex;
				auto CurrentSectionName = SectionHelper.GetSectionInfo(SectionIndex, MemberStartIndex, MemberEndIndex);

				TreeNodeState NodeState = {};
				NodeState.OverrideNoTreePush = true;
				BeginSection(CurrentSectionName, NodeState, State, StackIndex, SectionIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);

				if (NodeState.IsOpen)
					for (int MemberIndex = MemberStartIndex; MemberIndex < MemberEndIndex; MemberIndex++)
						DrawItemRow(State, Members[MemberIndex], CurrentMemberPath, StackIndex + 1);

				EndSection(NodeState, State);
			}
		}
	}

	// Functions.
	if (State.ListFunctionsOnObjectItems && Item.Ptr && ItemIsObject) {
		TArray<UFunction*> Functions = GetObjectFunctionList((UObject*)Item.Ptr);

		if (Functions.Num()) {
			TreeNodeState FunctionSection = {};
			BeginSection("Functions", FunctionSection, State, StackIndex, 0);

			if (FunctionSection.IsOpen) {
				SectionHelper SectionHelper;
				if (State.EnableClassCategoriesOnObjectItems) {
					for (auto& Function : Functions)
						SectionHelper.Add(Function->GetOuterUClass()->GetFName());

					SectionHelper.Init();
				}

				if (!SectionHelper.Enabled) {
					for (auto It : Functions)
						DrawItemRow(State, MakeFunctionItem(Item.Ptr, It), CurrentMemberPath, StackIndex + 1);

				} else {
					for (int SectionIndex = 0; SectionIndex < SectionHelper.GetSectionCount(); SectionIndex++) {
						int MemberStartIndex, MemberEndIndex;
						auto CurrentSectionName = SectionHelper.GetSectionInfo(SectionIndex, MemberStartIndex, MemberEndIndex);

						TreeNodeState NodeState = {};
						NodeState.OverrideNoTreePush = true;
						BeginSection(CurrentSectionName, NodeState, State, StackIndex, SectionIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);

						if (NodeState.IsOpen)
							for (int MemberIndex = MemberStartIndex; MemberIndex < MemberEndIndex; MemberIndex++)
								DrawItemRow(State, MakeFunctionItem(Item.Ptr, Functions[MemberIndex]), CurrentMemberPath, StackIndex + 1);

						EndSection(NodeState, State);
					}
				}
			}

			EndSection(FunctionSection, State);
		}
	}
}

bool ItemHasMetaData(PropertyItem& Item) {
	if (!Item.Prop)
		return false;

	bool Result = false;
#if MetaData_Available
	if (const TMap<FName, FString>* MetaData = Item.Prop->GetMetaDataMap())
		Result = (bool)MetaData->Num();
#endif

	return Result;
}

FAView GetColumnCellText(PropertyItem& Item, int ColumnID, TreeState* State, TInlineComponentArray<FAView>* CurrentMemberPath, int* StackIndex) {
	FAView Result = "";
	if (ColumnID == ColumnID_Name) {
		if (CurrentMemberPath && StackIndex) {
			bool TopLevelWatchListItem = State->CurrentWatchItemIndex != -1 && (*StackIndex) == 0;
			bool PathIsEditable = TopLevelWatchListItem && State->PathStringPtr;

			if (!PathIsEditable) {
				if (State->ForceInlineChildItems && State->InlineStackIndexLimit) {
					TMemBuilder(Builder);
					for (int i = State->InlineMemberPathIndexOffset; i < CurrentMemberPath->Num(); i++)
						Builder.Appendf("%s.", (*CurrentMemberPath)[i].GetData());
					Builder.Append(Item.GetAuthoredName());
					Result = *Builder;

				} else
					Result = Item.GetAuthoredName();
			}
		}

	} else if (ColumnID == ColumnID_Value) {
		Result = GetValueStringFromItem(Item);

	} else if (ColumnID == ColumnID_Metadata && Item.Prop) {
#if MetaData_Available
		if (const TMap<FName, FString>* MetaData = Item.Prop->GetMetaDataMap()) {
			TArray<FName> Keys;
			MetaData->GenerateKeyArray(Keys);
			TStringBuilder<256> Builder;

			int i = -1;
			for (auto Key : Keys) {
				i++;
				if (i != 0)
					Builder.Append("\n\n");
				Builder.Appendf(TEXT("%s:\n\t"), *Key.ToString());
				Builder.Append(*MetaData->Find(Key));
			}
			Result = TMem.CToA(*Builder, Builder.Len());
		}
#endif

	} else if (ColumnID == ColumnID_Type) {
		Result = Item.GetPropertyType();

	} else if (ColumnID == ColumnID_Cpptype) {
		Result = Item.GetCPPType();

	} else if (ColumnID == ColumnID_Class) {
		if (Item.Prop) {
			FFieldVariant Owner = ((FField*)Item.Prop)->Owner;
			Result = TMem.NToA(Owner.GetFName());

		} else if (Item.Type == PointerType::Function) {
			UClass* Class = ((UFunction*)Item.StructPtr)->GetOuterUClass();
			if (Class)
				Result = TMem.NToA(Class->GetFName());
		}

	} else if (ColumnID == ColumnID_Category) {
		Result = GetItemMetadataCategory(Item);

	} else if (ColumnID == ColumnID_Address) {
		Result = TMem.Printf("0x%" PRIXPTR "\n", (uintptr_t)Item.Ptr);

	} else if (ColumnID == ColumnID_Size) {
		int Size = Item.GetSize();
		if (Size != -1)
			Result = TMem.Printf("%d B", Size);
	}

	return Result;
}

FAView GetValueStringFromItem(PropertyItem& Item) {
	// Maybe we could just serialize the property to string?
	// Since we don't handle that many types for now we can just do it by hand.
	FAView Result;

	if (!Item.Ptr)
		Result = "Null";

	else if (!Item.Prop)
		Result = "";

	else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Item.Prop))
		Result = TMem.SToA(NumericProp->GetNumericPropertyValueToString(Item.Ptr));

	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Item.Prop))
		Result = ((bool*)(Item.Ptr)) ? "true" : "false";

	else if (Item.Prop->IsA(FStrProperty::StaticClass()))
		Result = TMem.SToA(*(FString*)Item.Ptr);

	else if (Item.Prop->IsA(FNameProperty::StaticClass()))
		Result = TMem.NToA(*((FName*)Item.Ptr));

	else if (Item.Prop->IsA(FTextProperty::StaticClass()))
		Result = TMem.SToA((FString&)((FText*)Item.Ptr)->ToString());

	return Result;
}

void DrawPropertyValue(PropertyItem& Item) {
	static TArray<char> StringBuffer;
	static int IntStep = 1;
	static int IntStepFast8 = 10;
	static int IntStepFast = 100;
	static int64 Int64Step = 1;
	static int64 Int64StepFast = 100;
	//static float FloatStepFast = 1;
	//static double DoubleStepFast = 100;

	bool DragEnabled = ImGui::IsKeyDown(ImGuiMod_Alt);

	if (Item.Ptr == 0) {
		ImGui::Text("<Null>");

	} else if (Item.Prop == 0) {
		ImGui::Text("{%d}", Item.GetMemberCount());

	} else if (FClassProperty* ClassProp = CastField<FClassProperty>(Item.Prop)) {
		UClass* Class = (UClass*)Item.Ptr;
		ImGui::Text(ImGui_StoA(*Class->GetAuthoredName()));

	} else if (FClassPtrProperty* ClassPtrProp = CastField<FClassPtrProperty>(Item.Prop)) {
		ImGui::Text("<Not Implemented>");

	} else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Item.Prop)) {
		FSoftObjectPtr* SoftClass = (FSoftObjectPtr*)Item.Ptr;
		if (SoftClass->IsStale())
			ImGui::Text("<Stale>");
		else if (SoftClass->IsPending())
			ImGui::Text("<Pending>");
		else if (!SoftClass->IsValid())
			ImGui::Text("<Null>");
		else {
			FString Path = SoftClass->ToSoftObjectPath().ToString();
			if (Path.Len())
				ImGuiAddon::InputString("##SoftClassProp", Path, StringBuffer);
		}

	} else if (FWeakObjectProperty* WeakObjProp = CastField<FWeakObjectProperty>(Item.Prop)) {
		TWeakObjectPtr<UObject>* WeakPtr = (TWeakObjectPtr<UObject>*)Item.Ptr;
		if (WeakPtr->IsStale())
			ImGui::Text("<Stale>");
		if (!WeakPtr->IsValid())
			ImGui::Text("<Null>");
		else {
			auto NewItem = MakeObjectItem(WeakPtr->Get());
			DrawPropertyValue(NewItem);
		}

	} else if (FLazyObjectProperty* LayzObjProp = CastField<FLazyObjectProperty>(Item.Prop)) {
		TLazyObjectPtr<UObject>* LazyPtr = (TLazyObjectPtr<UObject>*)Item.Ptr;
		if (LazyPtr->IsStale())
			ImGui::Text("<Stale>");
		if (!LazyPtr->IsValid())
			ImGui::Text("<Null>");
		else {
			auto NewItem = MakeObjectItem(LazyPtr->Get());
			DrawPropertyValue(NewItem);
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		FString ID = LazyPtr->GetUniqueID().ToString();
		ImGuiAddon::InputString("##LazyObjectProp", ID, StringBuffer);

	} else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Item.Prop)) {
		TSoftObjectPtr<UObject>* SoftObjPtr = (TSoftObjectPtr<UObject>*)Item.Ptr;
		if (SoftObjPtr->IsPending())
			ImGui::Text("<Pending>");
		else if (!SoftObjPtr->IsValid())
			ImGui::Text("<Null>");
		else {
			FString Path = SoftObjPtr->ToSoftObjectPath().ToString();
			if (Path.Len())
				ImGuiAddon::InputString("##SoftObjProp", Path, StringBuffer);
		}

	} else if (Item.Type == PointerType::Function) {
		if (ImGui::Button("Call Function")) {
			UObject* Obj = (UObject*)Item.Ptr;
			UFunction* Function = (UFunction*)Item.StructPtr;
			static char buf[256];
			Obj->ProcessEvent(Function, buf);
		}

	} else if (Item.Type == PointerType::Object ||
		Item.Prop->IsA(FObjectProperty::StaticClass())) {
		int MemberCount = Item.GetMemberCount();
		if (MemberCount)
			ImGui::Text("{%d}", MemberCount);
		else
			ImGui::Text("{}");

	} else if (CastField<FByteProperty>(Item.Prop) && CastField<FByteProperty>(Item.Prop)->IsEnum()) {
		FByteProperty* ByteProp = CastField<FByteProperty>(Item.Prop);
		if (ByteProp->Enum) {
			UEnum* Enum = ByteProp->Enum;
			int Count = Enum->NumEnums();

			StringBuffer.Empty();
			for (int i = 0; i < Count; i++) {
				FString Name = Enum->GetNameStringByIndex(i);
				StringBuffer.Append(ImGui_StoA(*Name), Name.Len());
				StringBuffer.Push('\0');
			}
			StringBuffer.Push('\0');
			uint8* Value = (uint8*)Item.Ptr;
			int TempInt = *Value;
			if (ImGui::Combo("##Enum", &TempInt, StringBuffer.GetData(), Count))
				*Value = TempInt;
		}

	} else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Item.Prop)) {
		bool TempBool = BoolProp->GetPropertyValue(Item.Ptr);
		ImGui::Checkbox("", &TempBool);
		BoolProp->SetPropertyValue(Item.Ptr, TempBool);

	} else if (Item.Prop->IsA(FInt8Property::StaticClass())) {
		ImGui::InputScalar("##FInt8Property", ImGuiDataType_S8, Item.Ptr, &IntStep, &IntStepFast8);

	} else if (Item.Prop->IsA(FByteProperty::StaticClass())) {
		ImGui::InputScalar("##FByteProperty", ImGuiDataType_U8, Item.Ptr, &IntStep, &IntStepFast8);

	} else if (Item.Prop->IsA(FInt16Property::StaticClass())) {
		ImGui::InputScalar("##FInt16Property", ImGuiDataType_S16, Item.Ptr, &IntStep, &IntStepFast);

	} else if (Item.Prop->IsA(FUInt16Property::StaticClass())) {
		ImGui::InputScalar("##FUInt16Property", ImGuiDataType_U16, Item.Ptr, &IntStep, &IntStepFast);

	} else if (Item.Prop->IsA(FIntProperty::StaticClass())) {
		ImGui::InputScalar("##FIntProperty", ImGuiDataType_S32, Item.Ptr, &IntStep, &IntStepFast);

	} else if (Item.Prop->IsA(FUInt32Property::StaticClass())) {
		ImGui::InputScalar("##FUInt32Property", ImGuiDataType_U32, Item.Ptr, &IntStep, &IntStepFast);

	} else if (Item.Prop->IsA(FInt64Property::StaticClass())) {
		ImGui::InputScalar("##FInt64Property", ImGuiDataType_S64, Item.Ptr, &Int64Step, &Int64StepFast);

	} else if (Item.Prop->IsA(FUInt64Property::StaticClass())) {
		ImGui::InputScalar("##FUInt64Property", ImGuiDataType_U64, Item.Ptr, &Int64Step, &Int64StepFast);

	} else if (Item.Prop->IsA(FFloatProperty::StaticClass())) {
		//ImGui::IsItemHovered
		//if(!DragEnabled)
		ImGui::InputFloat("##FFloatProperty", (float*)Item.Ptr);
		//else 
			//ImGui::DragFloat("##FFloatProperty", (float*)Item.Ptr, 1.0f);

	} else if (Item.Prop->IsA(FDoubleProperty::StaticClass())) {
		ImGui::InputDouble("##FDoubleProperty", (double*)Item.Ptr);

	} else if (Item.Prop->IsA(FStrProperty::StaticClass())) {
		ImGuiAddon::InputString("##StringProp", *(FString*)Item.Ptr, StringBuffer);

	} else if (Item.Prop->IsA(FNameProperty::StaticClass())) {
		FString Str = ((FName*)Item.Ptr)->ToString();
		if (ImGuiAddon::InputString("##NameProp", Str, StringBuffer)) (*((FName*)Item.Ptr)) = FName(Str);

	} else if (Item.Prop->IsA(FTextProperty::StaticClass())) {
		FString Str = ((FText*)Item.Ptr)->ToString();
		if (ImGuiAddon::InputString("##TextProp", Str, StringBuffer)) (*((FText*)Item.Ptr)) = FText::FromString(Str);

	} else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Item.Prop)) {
		FProperty* CurrentProp = ArrayProp->Inner;
		FScriptArrayHelper ScriptArrayHelper(ArrayProp, Item.Ptr);
		ImGui::Text("%s [%d]", ImGui_StoA(*CurrentProp->GetCPPType()), ScriptArrayHelper.Num());

	} else if (FMapProperty* MapProp = CastField<FMapProperty>(Item.Prop)) {
		FScriptMapHelper Helper = FScriptMapHelper(MapProp, Item.Ptr);
		ImGui::Text("<%s, %s> (%d)", ImGui_StoA(*MapProp->KeyProp->GetCPPType()), ImGui_StoA(*MapProp->ValueProp->GetCPPType()), Helper.Num());

	} else if (FSetProperty* SetProp = CastField<FSetProperty>(Item.Prop)) {
		FScriptSetHelper Helper = FScriptSetHelper(SetProp, Item.Ptr);
		ImGui::Text("<%s> {%d}", ImGui_StoA(*Helper.GetElementProperty()->GetCPPType()), Helper.Num());

	} else if (FMulticastDelegateProperty* MultiDelegateProp = CastField<FMulticastDelegateProperty>(Item.Prop)) {
		auto ScriptDelegate = (TMulticastScriptDelegate<FWeakObjectPtr>*)Item.Ptr;
		//MultiDelegateProp->SignatureFunction->
		//ScriptDelegate->ProcessMulticastDelegate(asdf);
		//auto BoundObjects = ScriptDelegate->GetAllObjects();
		//if (!MemberArray) return BoundObjects.Num();

		//for (auto Obj : BoundObjects)
		//	MemberArray->Push(MakeObjectItem(Obj));

		ImGui::BeginDisabled(); defer{ ImGui::EndDisabled(); };
		if (ImGui::Button("Broadcast")) {
			// @Todo
		}

	} else if (FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Item.Prop)) {
		auto ScriptDelegate = (TScriptDelegate<FWeakObjectPtr>*)Item.Ptr;
		FString Text;
		if (ScriptDelegate->IsBound()) Text = ScriptDelegate->GetFunctionName().ToString();
		else                           Text = "<No Function Bound>";
		ImGui::Text(ImGui_StoA(*Text));

	} else if (FMulticastInlineDelegateProperty* MultiInlineDelegateProp = CastField<FMulticastInlineDelegateProperty>(Item.Prop)) {
		ImGui::Text("<NotImplemented>"); // @Todo

	} else if (FMulticastSparseDelegateProperty* MultiSparseDelegateProp = CastField<FMulticastSparseDelegateProperty>(Item.Prop)) {
		ImGui::Text("<NotImplemented>"); // @Todo

	} else if (FStructProperty* StructProp = CastField<FStructProperty>(Item.Prop)) {
		FString Extended;
		FString StructType = StructProp->GetCPPType(&Extended, 0);

		if (StructType == "FVector") {
			ImGui::InputScalarN("##FVector", ImGuiDataType_Double, &((FVector*)Item.Ptr)->X, 3);

		} else if (StructType == "FRotator") {
			ImGui::InputScalarN("##FVector", ImGuiDataType_Double, &((FRotator*)Item.Ptr)->Pitch, 3);

		} else if (StructType == "FVector2D") {
			ImGui::InputScalarN("##FVector2D", ImGuiDataType_Double, &((FVector2D*)Item.Ptr)->X, 2);

		} else if (StructType == "FIntVector") {
			ImGui::InputInt3("##FIntVector", &((FIntVector*)Item.Ptr)->X);

		} else if (StructType == "FIntPoint") {
			ImGui::InputInt2("##FIntPoint", &((FIntPoint*)Item.Ptr)->X);

		} else if (StructType == "FTimespan") {
			FString s = ((FTimespan*)Item.Ptr)->ToString();
			if (ImGuiAddon::InputString("##FTimespan", s, StringBuffer))
				FTimespan::Parse(s, *((FTimespan*)Item.Ptr));

		} else if (StructType == "FDateTime") {
			FString s = ((FDateTime*)Item.Ptr)->ToString();
			if (ImGuiAddon::InputString("##FDateTime", s, StringBuffer))
				FDateTime::Parse(s, *((FDateTime*)Item.Ptr));

		} else if (StructType == "FLinearColor") {
			FLinearColor* lCol = (FLinearColor*)Item.Ptr;
			FColor sCol = lCol->ToFColor(true);
			float c[4] = { sCol.R / 255.0f, sCol.G / 255.0f, sCol.B / 255.0f, sCol.A / 255.0f };
			if (ImGui::ColorEdit4("##FLinearColor", c, ImGuiColorEditFlags_AlphaPreview)) {
				sCol = FColor(c[0] * 255, c[1] * 255, c[2] * 255, c[3] * 255);
				*lCol = FLinearColor::FromSRGBColor(sCol);
			}

		} else if (StructType == "FColor") {
			FColor* sCol = (FColor*)Item.Ptr;
			float c[4] = { sCol->R / 255.0f, sCol->G / 255.0f, sCol->B / 255.0f, sCol->A / 255.0f };
			if (ImGui::ColorEdit4("##FColor", c, ImGuiColorEditFlags_AlphaPreview))
				*sCol = FColor(c[0] * 255, c[1] * 255, c[2] * 255, c[3] * 255);

		} else {
			ImGui::Text("{%d}", Item.GetMemberCount());
		}
	} else {
		ImGui::Text("<UnknownType>");
	}
}

bool MemberPath::UpdateItemFromPath(TArray<PropertyItem>& Items) {
	// Name is the "path" to the member. You can traverse through objects, structs and arrays.
	// E.g.: objectMember.<arrayIndex>.structMember.float/int/bool member

	TArray<FString> MemberArray;
	PathString.ParseIntoArray(MemberArray, TEXT("."));

	if (MemberArray.IsEmpty()) return false;

	bool SearchFailed = false;

	// Find first name in items.
	PropertyItem CurrentItem;
	{
		bool Found = false;
		for (auto& It : Items)
			if (FString(It.GetAuthoredName()) == MemberArray[0]) { //@Fix
				CurrentItem = It;
				Found = true;
			}

		if (!Found) SearchFailed = true;
		MemberArray.RemoveAt(0);
	}

	if (!SearchFailed)
		for (auto& MemberName : MemberArray) {
			//if (!CurrentItem->IsValid()) return false;

			// (Membername=Value)
			bool SearchByMemberValue = false;
			//FString MemberNameToTest;
			//FString MemberValueToTest;
			//if (MemberName[0] == '(' && MemberName[MemberName.Len()-1] == ')') {
			//	SearchByMemberValue = true;

			//	MemberName.RemoveAt(0);
			//	MemberName.RemoveAt(MemberName.Len() - 1);

			//	bool Result = MemberName.Split("=", &MemberNameToTest, &MemberValueToTest);
			//	if(!Result) { SearchFailed = true; break; }
			//}

			TArray<PropertyItem> Members;
			CurrentItem.GetMembers(&Members);
			bool Found = false;
			for (auto MemberItem : Members) {
				FString ItemName = FString(MemberItem.GetAuthoredName()); //@Fix

				if (SearchByMemberValue) {
					//if (ItemName == MemberNameToTest) {
					//	MemberItem.
					//}

				} else {
					if (ItemName == MemberName) {
						CurrentItem = MemberItem;
						Found = true;
						break;
					}
				}
			}
			if (!Found) {
				SearchFailed = true;
				break;
			}
		}


	if (!SearchFailed)
		CachedItem = CurrentItem;
	else
		CachedItem = {};

	// Have to set this either way, because we want to see the path in the watch window.
	CachedItem.NameOverwrite = TMem.SToA(PathString);

	return !SearchFailed;
}

FString ConvertWatchedMembersToString(TArray<MemberPath>& WatchedMembers) {
	TArray<FString> Strings;
	for (auto It : WatchedMembers)
		Strings.Push(It.PathString);

	FString Result = FString::Join(Strings, TEXT(","));
	return Result;
}

void LoadWatchedMembersFromString(FString Data, TArray<MemberPath>& WatchedMembers) {
	WatchedMembers.Empty();

	TArray<FString> Strings;
	Data.ParseIntoArray(Strings, TEXT(","));
	for (auto It : Strings) {
		MemberPath Path = {};
		Path.PathString = It;

		WatchedMembers.Push(Path);
	}
}

// -------------------------------------------------------------------------------------------

FName PropertyItem::GetName() {
	if (Type == PointerType::Property && Prop)
		return Prop->GetFName();

	if (Type == PointerType::Object && Ptr)
		return ((UObject*)Ptr)->GetFName();

	if (StructPtr && (Type == PointerType::Struct || Type == PointerType::Function))
		return StructPtr->GetFName();
		//return StructPtr->GetAuthoredName();

	return NAME_None;
}

FAView PropertyItem::GetAuthoredName() {
	return !NameOverwrite.IsEmpty() ? NameOverwrite : TMem.NToA(GetName());
}

//FString PropertyItem::GetDisplayName() {
//	FString Result = GetAuthoredName();
//	if (Type == PointerType::Property && CastField<FObjectProperty>(Prop) && Ptr)
//		Result = FString::Printf(TEXT("%s (%s)"), *Result, *((UObject*)Ptr)->GetName());
//
//	return Result;
//}

bool PropertyItem::IsExpandable() {
	if (!IsValid())
		return false;

	if (!Ptr || Type == PointerType::Function)
		return false;

	if (Type == PointerType::Object || Type == PointerType::Struct)
		return true;

	if (Prop->IsA(FArrayProperty::StaticClass()) ||
		Prop->IsA(FMapProperty::StaticClass()) ||
		Prop->IsA(FSetProperty::StaticClass()) ||
		Prop->IsA(FObjectProperty::StaticClass()) ||
		Prop->IsA(FWeakObjectProperty::StaticClass()) ||
		Prop->IsA(FLazyObjectProperty::StaticClass()) ||
		Prop->IsA(FSoftObjectProperty::StaticClass()))
		return true;

	if (Prop->IsA(FStructProperty::StaticClass())) {
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);

		// @Todo: These shouldn't be hardcoded.
		static TSet<FName> TypeSetX = {
			"Vector", "Rotator", "Vector2D", "IntVector2", "IntVector", "Timespan", "DateTime" 
		};
		
		FName StructType = StructProp->Struct->GetFName();
		bool Inlined = TypeSetX.Contains(StructType);
		return !Inlined;
	}

	if (Prop->IsA(FDelegateProperty::StaticClass()))
		return true;

	if (Prop->IsA(FMulticastDelegateProperty::StaticClass()))
		return true;

	return false;
}

FAView PropertyItem::GetPropertyType() {
	FAView Result = "";
	if (Type == PointerType::Property && Prop)
		Result = TMem.NToA(Prop->GetClass()->GetFName());

	else if (Type == PointerType::Object)
		Result = "";

	else if (Type == PointerType::Struct)
		Result = "";

	return Result;
};

FAView PropertyItem::GetCPPType() {
	if (Type == PointerType::Property && Prop) 
		return TMem.SToA(Prop->GetCPPType());

	if (Type == PointerType::Struct)           
		return TMem.SToA(((UScriptStruct*)StructPtr)->GetStructCPPName());

	if (Type == PointerType::Object && Ptr) {
		UClass* Class = ((UObject*)Ptr)->GetClass();
		if (Class) 
			return TMem.NToA(Class->GetFName());
	}

	// Do we really have to do this? Is there no engine function?
	if (Type == PointerType::Function) {
		UFunction* Function = (UFunction*)StructPtr;

		FProperty* ReturnProp = Function->GetReturnProperty();
		FString ts = ReturnProp ? ReturnProp->GetCPPType() : "void";

		ts += TEXT(" (");
		int i = 0;
		for (FProperty* MemberProp : TFieldRange<FProperty>(Function)) {
			if (MemberProp == ReturnProp) continue;
			if (i == 1) ts += TEXT(", ");
			ts += MemberProp->GetCPPType();
			i++;
		}
		ts += TEXT(")");

		return TMem.SToA(ts);
	}

	return "";
};

int PropertyItem::GetSize() {
	if (Prop) 
		return Prop->GetSize();

	else if (Type == PointerType::Object) {
		UClass* Class = ((UObject*)Ptr)->GetClass();
		if (Class) 
			return Class->GetPropertiesSize();

	} else if (Type == PointerType::Struct) 
		return StructPtr->GetPropertiesSize();

	return -1;
};

int PropertyItem::GetMembers(TArray<PropertyItem>* MemberArray) {
	if (!Ptr) return 0;

	int Count = 0;

	if (Type == PointerType::Object || CastField<FObjectProperty>(Prop)) {
		UClass* Class = ((UObject*)Ptr)->GetClass();
		if (!Class) return 0;
		for (FProperty* MemberProp : TFieldRange<FProperty>(Class)) {
			if (!MemberArray) { Count++; continue; }

			void* MemberPtr = ContainerToValuePointer(PointerType::Object, Ptr, MemberProp);
			MemberArray->Push(MakePropertyItem(MemberPtr, MemberProp));
		}

	} else if (Prop &&
		(Prop->IsA(FWeakObjectProperty::StaticClass()) ||
			Prop->IsA(FLazyObjectProperty::StaticClass()) ||
			Prop->IsA(FSoftObjectProperty::StaticClass()))) {
		UObject* Obj = 0;
		bool IsValid = GetObjFromObjPointerProp(*this, Obj);
		if (!IsValid) return 0;
		else return MakeObjectItem(Obj).GetMembers(MemberArray);

	} else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop)) {
		FScriptArrayHelper ScriptArrayHelper(ArrayProp, Ptr);

		if (!MemberArray) return ScriptArrayHelper.Num();
		FProperty* MemberProp = ArrayProp->Inner;
		for (int i = 0; i < ScriptArrayHelper.Num(); i++) {
			void* MemberPtr = ContainerToValuePointer(PointerType::Array, ScriptArrayHelper.GetRawPtr(i), MemberProp);
			MemberArray->Push(MakeArrayItem(MemberPtr, MemberProp, i));
		}

	} else if (Type == PointerType::Struct || CastField<FStructProperty>(Prop)) {
		if (!StructPtr)
			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				StructPtr = StructProp->Struct;

		if (StructPtr) {
			for (FProperty* MemberProp : TFieldRange<FProperty>(StructPtr)) {
				if (!MemberArray) { Count++; continue; }

				void* MemberPtr = ContainerToValuePointer(PointerType::Object, Ptr, MemberProp);
				MemberArray->Push(MakePropertyItem(MemberPtr, MemberProp));
			}
		}

	} else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop)) {
		FScriptMapHelper Helper = FScriptMapHelper(MapProp, Ptr);
		if (!MemberArray) return Helper.Num();

		auto KeyProp = Helper.GetKeyProperty();
		auto ValueProp = Helper.GetValueProperty();
		for (int i = 0; i < Helper.Num(); i++) {
			uint8* KeyPtr = Helper.GetKeyPtr(i);
			uint8* ValuePtr = Helper.GetValuePtr(i);
			void* ValuePtr2 = ContainerToValuePointer(PointerType::Map, ValuePtr, ValueProp);

			auto KeyItem = MakeArrayItem(KeyPtr, KeyProp, i);
			auto ValueItem = MakeArrayItem(ValuePtr2, ValueProp, i);
			TMem.Append(&KeyItem.NameOverwrite, " Key");
			TMem.Append(&ValueItem.NameOverwrite, " Value");
			MemberArray->Push(KeyItem);
			MemberArray->Push(ValueItem);
		}

	} else if (FSetProperty* SetProp = CastField<FSetProperty>(Prop)) {
		FScriptSetHelper Helper = FScriptSetHelper(SetProp, Ptr);
		if (!MemberArray) return Helper.Num();

		FProperty* MemberProp = Helper.GetElementProperty();
		for (int i = 0; i < Helper.Num(); i++) {
			void* MemberPtr = Helper.Set->GetData(i, Helper.SetLayout);
			MemberPtr = ContainerToValuePointer(PointerType::Array, MemberPtr, MemberProp);
			MemberArray->Push(MakeArrayItem(MemberPtr, MemberProp, i));
		}

	} else if (FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Prop)) {
		//DelegateProp->SignatureFunction
		auto ScriptDelegate = (TScriptDelegate<FWeakObjectPtr>*)Ptr;
		if (!MemberArray) return ScriptDelegate->IsBound() ? 1 : 0;
		if (ScriptDelegate->IsBound()) {
			UFunction* Function = ScriptDelegate->GetUObject()->FindFunction(ScriptDelegate->GetFunctionName());
			if (Function)
				MemberArray->Push(MakeFunctionItem(ScriptDelegate->GetUObject(), Function));
		}

	} else if (FMulticastDelegateProperty* c = CastField<FMulticastDelegateProperty>(Prop)) {
		// We would like to call GetAllObjects(), but can't because the invocation list can be invalid and so the function call would fail.
		// And since there is no way to check if the invocation list is invalid we can't handle this property.

		//auto ScriptDelegate = (TMulticastScriptDelegate<FWeakObjectPtr>*)Ptr;
		//if (ScriptDelegate->IsBound()) {
		//	auto BoundObjects = ScriptDelegate->GetAllObjects();
		//	if (!MemberArray) return BoundObjects.Num();

		//	for (auto Obj : BoundObjects)
		//		MemberArray->Push(MakeObjectItem(Obj));
		//} else
		//	if (!MemberArray) return 0;
	}

	return Count;
}

int PropertyItem::GetMemberCount() {
	if (CachedMemberCount != -1)
		return CachedMemberCount;

	CachedMemberCount = GetMembers(0);
	return CachedMemberCount;
}

void* ContainerToValuePointer(PointerType Type, void* ContainerPtr, FProperty* MemberProp) {
	switch (Type) {
	case Object: {
		if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(MemberProp))
			return ObjectProp->GetObjectPropertyValue_InContainer(ContainerPtr);
		else
			return MemberProp->ContainerPtrToValuePtr<void>(ContainerPtr);
	} break;
	case Array: {
		if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(MemberProp))
			return ObjectProp->GetObjectPropertyValue_InContainer(ContainerPtr);
		else
			return ContainerPtr; // Already memberPointer in the case of arrays.
	} break;
	case Map: {
		if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(MemberProp))
			return *(UObject**)ContainerPtr; // Not sure why this is different than from arrays.
		else
			return ContainerPtr; // Already memberPointer.
	} break;
	}

	return 0;
}

PropertyItem MakeObjectItem(void* _Ptr) {
	return { PointerType::Object, _Ptr };
}
PropertyItem MakeObjectItemNamed(void* _Ptr, const char* _NameOverwrite, FAView NameID) {
	return MakeObjectItemNamed(_Ptr, FAView(_NameOverwrite));
}
PropertyItem MakeObjectItemNamed(void* _Ptr, FString _NameOverwrite, FAView NameID) {
	TMem.Init(TMemoryStartSize);
	return MakeObjectItemNamed(_Ptr, TMem.SToA(_NameOverwrite));
}
PropertyItem MakeObjectItemNamed(void* _Ptr, FAView _NameOverwrite, FAView NameID) {
	return { PointerType::Object, _Ptr, 0, _NameOverwrite, 0, NameID };
}
PropertyItem MakeArrayItem(void* _Ptr, FProperty* _Prop, int _Index, bool IsObjectProp) {
	return { PointerType::Property, _Ptr, _Prop, TMem.Printf("[%d]", _Index) };
}
PropertyItem MakePropertyItem(void* _Ptr, FProperty* _Prop) {
	return { PointerType::Property, _Ptr, _Prop };
}
PropertyItem MakePropertyItemNamed(void* _Ptr, FProperty* _Prop, FAView Name, FAView NameID) {
	return { PointerType::Property, _Ptr, _Prop, Name, 0, NameID };
}
PropertyItem MakeFunctionItem(void* _Ptr, UFunction* _Function) {
	return { PointerType::Function, _Ptr, 0, "", _Function };
}

// -------------------------------------------------------------------------------------------

void SetTableRowBackgroundByStackIndex(int StackIndex) {
	if (StackIndex == 0)
		return;

	// Cache colors.
	static ImU32 Colors[4] = {};
	static bool Init = true;
	if (Init) {
		Init = false;
		ImVec4 c = { 0, 0, 0, 0.05f };
		for (int i = 0; i < 4; i++) {
			float h = (i / 4.0f) + 0.065; // Start with orange, cycle 4 colors.
			ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, c.x, c.y, c.z);
			Colors[i] = ImGui::GetColorU32(c);
		}
	}

	ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, Colors[(StackIndex-1)%4]);
}

int GetDigitKeyDownAsInt() {
	for (int i = 0; i < 10; i++) {
		ImGuiKey DigitKeyCode = (ImGuiKey)(ImGuiKey_0 + i);
		if (ImGui::IsKeyDown(DigitKeyCode))
			return i;
	}
	return 0;
}

bool BeginTreeNode(const char* NameID, const char* DisplayName, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags) {
	bool IsNameNodeVisible = State.IsCurrentItemVisible();
	bool ItemIsInlined = false;

	if (NodeState.HasBranches) {
		ItemIsInlined = State.ForceInlineChildItems && (StackIndex <= State.InlineStackIndexLimit);

		if (State.ForceInlineChildItems && State.ItemIsInfiniteLooping(NodeState.ItemInfo))
			ItemIsInlined = false;
	}

	if (ItemIsInlined) {
		NodeState.IsOpen = true;
		ImGui::TreePush(NameID);
		ImGui::Unindent();
		NodeState.ItemIsInlined = true;

	} else {
		// Start new row. Column index before this should be LastColumnIndex + 1.
		ImGui::TableNextColumn();

		if (IsNameNodeVisible) {
			ImGui::AlignTextToFramePadding();
			SetTableRowBackgroundByStackIndex(NodeState.VisualStackIndex != -1 ? NodeState.VisualStackIndex : StackIndex);
		}
		bool PushTextColor = IsNameNodeVisible && NodeState.PushTextColor;
		if (PushTextColor)
			ImGui::PushStyleColor(ImGuiCol_Text, NodeState.TextColor);

		bool NodeStateChanged = false;

		if (NodeState.HasBranches) {
			// If force open mode is active we change the state of the node if needed.
			if (State.ForceToggleNodeOpenClose) {
				auto StateStorage = ImGui::GetStateStorage();
				auto ID = ImGui::GetID(NameID);
				bool IsOpen = (bool)StateStorage->GetInt(ID);

				// Tree node state should change.
				if (State.ForceToggleNodeMode != IsOpen) {
					bool StateChangeAllowed = true;

					// Checks when trying to toggle open node.
					if (State.ForceToggleNodeMode) {
						// Address already visited.
						if (State.ItemIsInfiniteLooping(NodeState.ItemInfo))
							StateChangeAllowed = false;

						// Stack depth limit reached.
						if (StackIndex > State.ForceToggleNodeStackIndexLimit)
							StateChangeAllowed = false;
					}

					if (StateChangeAllowed) {
						ImGui::SetNextItemOpen(State.ForceToggleNodeMode);
						NodeStateChanged = true;
					}
				}
			}

			int Flags = ExtraFlags | ImGuiTreeNodeFlags_NavLeftJumpsBackHere;
			if (NodeState.OverrideNoTreePush)
				Flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen;

			const char* DisplayText = IsNameNodeVisible ? DisplayName : "";
			NodeState.IsOpen = ImGui::TreeNodeEx(NameID, Flags, DisplayText);

			{
				NodeState.ActivatedForceToggleNodeOpenClose = false;

				// Start force toggle mode.
				if (ImGui::IsItemToggledOpen() && ImGui::IsKeyDown(ImGuiMod_Shift) && !State.ForceToggleNodeOpenClose) {
					NodeState.ActivatedForceToggleNodeOpenClose = true;
					int StackLimitOffset = GetDigitKeyDownAsInt();
					State.EnableForceToggleNode(NodeState.IsOpen, StackIndex + (StackLimitOffset == 0 ? 10 : StackLimitOffset));
				}

				// If we forced this node closed we have to draw it's children for one frame so they can be forced closed as well.
				// The goal is to close everything that's visually open.
				if (State.IsForceToggleNodeActive(StackIndex) && (NodeState.ActivatedForceToggleNodeOpenClose || NodeStateChanged) && !NodeState.IsOpen) {
					if (!(Flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
						ImGui::TreePush(NameID);

					NodeState.IsOpen = true;
				}
			}

		} else {
			const char* DisplayText = IsNameNodeVisible ? DisplayName : "";
			ImGui::TreeNodeEx(DisplayText, ExtraFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
		}

		if (PushTextColor)
			ImGui::PopStyleColor(1);
	}

	return NodeState.IsOpen;
}

void TreeNodeSetInline(TreeNodeState& NodeState, TreeState& State, int CurrentMemberPathLength, int StackIndex, int InlineStackDepth) {
	NodeState.InlineChildren = true;

	State.ForceInlineChildItems = true;
	// @Note: Should we enable inifinite inlining again in the future?
	//State.InlineStackIndexLimit = InlineStackDepth == 0 ? StackIndex + 100 : StackIndex + InlineStackDepth;
	State.InlineStackIndexLimit = StackIndex + InlineStackDepth;

	State.InlineMemberPathIndexOffset = CurrentMemberPathLength;
	State.VisitedPropertiesStack.Empty();
}

void EndTreeNode(TreeNodeState& NodeState, TreeState& State) {
	if (NodeState.ItemIsInlined) {
		ImGui::TreePop();
		ImGui::Indent();

	} else if (NodeState.IsOpen && !NodeState.OverrideNoTreePush)
		ImGui::TreePop();

	if (NodeState.ActivatedForceToggleNodeOpenClose)
		State.DisableForceToggleNode();

	if (NodeState.InlineChildren)
		State.ForceInlineChildItems = false;
}

bool BeginSection(FAView Name, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags) {
	bool NodeOpenCloseLogicIsEnabled = true;

	bool ItemIsVisible = State.IsCurrentItemVisible();

	NodeState.HasBranches = true;

	if (ItemIsVisible) {
		NodeState.PushTextColor = true;
		NodeState.TextColor = ImVec4(1, 1, 1, 0.5f);
		NodeState.VisualStackIndex = StackIndex + 1;

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0));
	}

	ExtraFlags |= ImGuiTreeNodeFlags_Framed;
	bool IsOpen = BeginTreeNode(*Name, ItemIsVisible ? *TMem.Printf("(%s)", *Name) : "", NodeState, State, StackIndex, ExtraFlags);

	// Nothing else is drawn in the row so we skip to the next one.
	ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);

	if(ItemIsVisible)
		ImGui::PopStyleColor(1);

	return IsOpen;
}

void EndSection(TreeNodeState& NodeState, TreeState& State) {
	EndTreeNode(NodeState, State);
}

TArray<FName> GetClassFunctionList(UClass* Class) {
	TArray<FName> FunctionNames;

#if WITH_EDITOR
	Class->GenerateFunctionList(FunctionNames);
#else
	{
		// Hack to get at function data in shipping builds.
		struct TempStruct {
			TMap<FName, UFunction*> FuncMap;
			TMap<FName, UFunction*> SuperFuncMap;
			FRWLock SuperFuncMapLock;
			TArray<FImplementedInterface> Interfaces;
		};

		int Offset = offsetof(UClass, Interfaces) - offsetof(TempStruct, Interfaces);
		auto FuncMap = (TMap<FName, UFunction*>*)(((char*)Class) + Offset);
		if (FuncMap)
			FuncMap->GenerateKeyArray(FunctionNames);
	}
#endif

	return FunctionNames;
}

TArray<UFunction*> GetObjectFunctionList(UObject* Obj) {
	TArray<UFunction*> Functions;

	UClass* Class = Obj->GetClass();
	if (!Class)
		return Functions;

	UClass* TempClass = Class;
	do {
		TArray<FName> FunctionNames = GetClassFunctionList(TempClass);
		for (auto& Name : FunctionNames) {
			UFunction* Function = Class->FindFunctionByName(Name);
			if (!Function)
				continue;
			Functions.Push(Function);
		}
		TempClass = TempClass->GetSuperClass();

	} while (TempClass->GetSuperClass());

	return Functions;
}

FAView GetItemMetadataCategory(PropertyItem& Item) {
	FAView Category = "";
#if WITH_EDITORONLY_DATA											
	if (Item.Prop) {
		if (const TMap<FName, FString>* MetaData = Item.Prop->GetMetaDataMap()) {
			if (const FString* Value = MetaData->Find("Category"))
				Category = TMem.SToA(*((FString*)Value));
		}
	}
#endif
	return Category;
}

bool GetItemColor(PropertyItem& Item, ImVec4& Color) {
	FLinearColor lColor = {};
	lColor.R = -1;

	// Copied from GraphEditorSettings.cpp
	static FLinearColor DefaultPinTypeColor(0.750000f, 0.6f, 0.4f, 1.0f);              // light brown
	static FLinearColor ExecutionPinTypeColor(1.0f, 1.0f, 1.0f, 1.0f);                 // white
	static FLinearColor BooleanPinTypeColor(0.300000f, 0.0f, 0.0f, 1.0f);              // maroon
	static FLinearColor BytePinTypeColor(0.0f, 0.160000f, 0.131270f, 1.0f);            // dark green
	static FLinearColor ClassPinTypeColor(0.1f, 0.0f, 0.5f, 1.0f);                     // deep purple (violet
	static FLinearColor IntPinTypeColor(0.013575f, 0.770000f, 0.429609f, 1.0f);        // green-blue
	static FLinearColor Int64PinTypeColor(0.413575f, 0.770000f, 0.429609f, 1.0f);
	static FLinearColor FloatPinTypeColor(0.357667f, 1.0f, 0.060000f, 1.0f);           // bright green
	static FLinearColor DoublePinTypeColor(0.039216f, 0.666667f, 0.0f, 1.0f);          // darker green
	static FLinearColor RealPinTypeColor(0.039216f, 0.666667f, 0.0f, 1.0f);            // darker green
	static FLinearColor NamePinTypeColor(0.607717f, 0.224984f, 1.0f, 1.0f);            // lilac
	static FLinearColor DelegatePinTypeColor(1.0f, 0.04f, 0.04f, 1.0f);                // bright red
	static FLinearColor ObjectPinTypeColor(0.0f, 0.4f, 0.910000f, 1.0f);               // sharp blue
	static FLinearColor SoftObjectPinTypeColor(0.3f, 1.0f, 1.0f, 1.0f);
	static FLinearColor SoftClassPinTypeColor(1.0f, 0.3f, 1.0f, 1.0f);
	static FLinearColor InterfacePinTypeColor(0.8784f, 1.0f, 0.4f, 1.0f);              // pale green
	static FLinearColor StringPinTypeColor(1.0f, 0.0f, 0.660537f, 1.0f);               // bright pink
	static FLinearColor TextPinTypeColor(0.8f, 0.2f, 0.4f, 1.0f);                      // salmon (light pink
	static FLinearColor StructPinTypeColor(0.0f, 0.1f, 0.6f, 1.0f);                    // deep blue
	static FLinearColor WildcardPinTypeColor(0.220000f, 0.195800f, 0.195800f, 1.0f);   // dark gray
	static FLinearColor VectorPinTypeColor(1.0f, 0.591255f, 0.016512f, 1.0f);          // yellow
	static FLinearColor RotatorPinTypeColor(0.353393f, 0.454175f, 1.0f, 1.0f);         // periwinkle
	static FLinearColor TransformPinTypeColor(1.0f, 0.172585f, 0.0f, 1.0f);            // orange
	static FLinearColor IndexPinTypeColor(0.013575f, 0.770000f, 0.429609f, 1.0f);      // green-blue

	if (!Item.Prop)
		return false;

	if (Item.Prop->IsA(FBoolProperty::StaticClass()))      lColor = BooleanPinTypeColor;
	else if (Item.Prop->IsA(FByteProperty::StaticClass()))      lColor = BytePinTypeColor;
	else if (Item.Prop->IsA(FClassProperty::StaticClass()))     lColor = ClassPinTypeColor;
	else if (Item.Prop->IsA(FIntProperty::StaticClass()))       lColor = IntPinTypeColor;
	else if (Item.Prop->IsA(FInt64Property::StaticClass()))     lColor = Int64PinTypeColor;
	else if (Item.Prop->IsA(FFloatProperty::StaticClass()))     lColor = FloatPinTypeColor;
	else if (Item.Prop->IsA(FDoubleProperty::StaticClass()))    lColor = DoublePinTypeColor;
	//else if (Item.Prop->IsA(FRealproperty::StaticClass()))    lColor = ;
	else if (Item.Prop->IsA(FNameProperty::StaticClass()))      lColor = NamePinTypeColor;
	else if (Item.Prop->IsA(FDelegateProperty::StaticClass()))  lColor = DelegatePinTypeColor;
	else if (Item.Prop->IsA(FObjectProperty::StaticClass()))    lColor = ObjectPinTypeColor;
	else if (Item.Prop->IsA(FSoftClassProperty::StaticClass())) lColor = SoftClassPinTypeColor;
	else if (Item.Prop->IsA(FInterfaceProperty::StaticClass())) lColor = InterfacePinTypeColor;
	else if (Item.Prop->IsA(FStrProperty::StaticClass()))       lColor = StringPinTypeColor;
	else if (Item.Prop->IsA(FTextProperty::StaticClass()))      lColor = TextPinTypeColor;

	else if (FStructProperty* StructProp = CastField<FStructProperty>(Item.Prop)) {
		FString Extended;
		FString StructType = StructProp->GetCPPType(&Extended, 0);
		if (StructType == "FVector")    lColor = VectorPinTypeColor;
		else if (StructType == "FRotator")   lColor = RotatorPinTypeColor;
		else if (StructType == "FTransform") lColor = TransformPinTypeColor;
		else                                 lColor = StructPinTypeColor;
	}

	bool ColorGotSet = lColor.R != -1;
	if (ColorGotSet) {
		FColor c = lColor.ToFColor(true);
		Color = { c.R / 255.0f, c.G / 255.0f, c.B / 255.0f, c.A / 255.0f };
		return true;
	}

	return false;
}

bool GetObjFromObjPointerProp(PropertyItem& Item, UObject*& Object) {
	if (Item.Prop &&
		(Item.Prop->IsA(FWeakObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FLazyObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FSoftObjectProperty::StaticClass()))) {
		bool IsValid = false;
		UObject* Obj = 0;
		if (Item.Prop->IsA(FWeakObjectProperty::StaticClass())) {
			IsValid = ((TWeakObjectPtr<UObject>*)Item.Ptr)->IsValid();
			Obj = ((TWeakObjectPtr<UObject>*)Item.Ptr)->Get();

		} else if (Item.Prop->IsA(FLazyObjectProperty::StaticClass())) {
			IsValid = ((TLazyObjectPtr<UObject>*)Item.Ptr)->IsValid();
			Obj = ((TLazyObjectPtr<UObject>*)Item.Ptr)->Get();

		} else if (Item.Prop->IsA(FSoftObjectProperty::StaticClass())) {
			IsValid = ((TSoftObjectPtr<UObject>*)Item.Ptr)->IsValid();
			Obj = ((TSoftObjectPtr<UObject>*)Item.Ptr)->Get();
		}
		Object = Obj;
		return IsValid;
	}

	return false;
}

// -------------------------------------------------------------------------------------------

void SimpleSearchParser::ParseExpression(FAView str, TArray<FAView> _Columns) {
	Commands.Empty();

	struct StackInfo {
		TArray<Test> Tests = { {} };
		TArray<Operator> OPs;
	};
	TArray<StackInfo> Stack = { {} };

	auto EatToken = [&str](FAView Token) -> bool {
		if (str.StartsWith(Token)) {
			str.RemovePrefix(Token.Len());
			return true;
		}
		return false;
	};

	auto PushedWord = [&]() {
		for (int i = Stack.Last().OPs.Num() - 1; i >= 0; i--)
			Commands.Push({ Command_Op, {}, Stack.Last().OPs[i] });

		if (Stack.Last().Tests.Num() > 1)
			if (!Stack.Last().OPs.Contains(OP_Or))
				Commands.Push({ Command_Op, {}, OP_And });

		Stack.Last().OPs.Empty();
		Stack.Last().Tests.Push({});
	};

	while (true) {
		str.TrimStartInline();
		if (str.IsEmpty()) break;

		if      (EatToken("|"))  Stack.Last().OPs.Push(OP_Or);
		else if (EatToken("!"))  Stack.Last().OPs.Push(OP_Not);
		else if (EatToken("+"))  Stack.Last().Tests.Last().Mod = Mod_Exact;
		else if (EatToken("r:")) Stack.Last().Tests.Last().Mod = Mod_Regex;
		else if (EatToken("<=")) Stack.Last().Tests.Last().Mod = Mod_LessEqual;
		else if (EatToken(">=")) Stack.Last().Tests.Last().Mod = Mod_GreaterEqual;
		else if (EatToken("<"))  Stack.Last().Tests.Last().Mod = Mod_Less;
		else if (EatToken(">"))  Stack.Last().Tests.Last().Mod = Mod_Greater;
		else if (EatToken("="))  Stack.Last().Tests.Last().Mod = Mod_Equal;
		else if (EatToken("("))  Stack.Push({});
		else if (EatToken(")")) {
			Stack.Pop();
			if (!Stack.Num()) break;
			PushedWord();

		} else {
			// Column Name
			{
				bool Found = false;
				for (int i = 0; i < _Columns.Num(); i++) {
					auto Col = _Columns[i];
					if (str.StartsWith(Col, ESearchCase::IgnoreCase)) {
						if (str.RightChop(Col.Len()).StartsWith(':')) {
							Stack.Last().Tests.Last().ColumnID = i; // ColumnID should always be the index for this to work.
							str.RemovePrefix(Col.Len() + 1);
							Found = true;
							break;
						}
					}
				}
				if (Found) continue;
			}

			if (EatToken("\"")) {
				int Index;
				if (str.FindChar('"', Index)) {
					Stack.Last().Tests.Last().Ident = str.Left(Index);
					str.RemovePrefix(Index + 1);

					Commands.Push({ Command_Test, Stack.Last().Tests.Last() });
					PushedWord();
				}
				continue;
			}

			// Word
			{
				int Index = 0;
				while (Index < str.Len() && ((str[Index] >= 'A' && str[Index] <= 'Z') || (str[Index] >= 'a' && str[Index] <= 'z') ||
					(str[Index] >= '0' && str[Index] <= '9') || str[Index] == '_'))
					Index++;

				// For now we skip chars we don't know.
				if (!Index) {
					str.RemovePrefix(1);
					continue;
				}

				Stack.Last().Tests.Last().Ident = str.Left(Index);
				str.RemovePrefix(Index);

				Commands.Push({ Command_Test, Stack.Last().Tests.Last() });
				PushedWord();
			}
		}
	}
}

bool SimpleSearchParser::ApplyTests(CachedColumnText& ColumnTexts) {
	TArray<bool> Bools;
	for (auto Command : Commands) {
		if (Command.Type == Command_Test) {
			Test& Tst = Command.Tst;
			FAView* FoundString = ColumnTexts.Get(Tst.ColumnID);
			if (!FoundString)
				continue;
			FAView ColStr = *FoundString;

			bool Result;
			if     (!Tst.Mod)                     Result = StringView_Contains<ANSICHAR>(ColStr, Tst.Ident);
			else if (Tst.Mod == Mod_Exact)        Result = ColStr.Equals(Tst.Ident, ESearchCase::IgnoreCase);
			else if (Tst.Mod == Mod_Equal)        Result = ColStr.Len() ? FCStringAnsi::Atod(*ColStr) == FCStringAnsi::Atod(*Tst.Ident) : false;
			else if (Tst.Mod == Mod_Greater)      Result = ColStr.Len() ? FCStringAnsi::Atod(*ColStr) >  FCStringAnsi::Atod(*Tst.Ident) : false;
			else if (Tst.Mod == Mod_Less)         Result = ColStr.Len() ? FCStringAnsi::Atod(*ColStr) <  FCStringAnsi::Atod(*Tst.Ident) : false;
			else if (Tst.Mod == Mod_GreaterEqual) Result = ColStr.Len() ? FCStringAnsi::Atod(*ColStr) >= FCStringAnsi::Atod(*Tst.Ident) : false;
			else if (Tst.Mod == Mod_LessEqual)    Result = ColStr.Len() ? FCStringAnsi::Atod(*ColStr) <= FCStringAnsi::Atod(*Tst.Ident) : false;

			else if (Tst.Mod == Mod_Regex) {
				FRegexMatcher RegMatcher(FRegexPattern(*Tst.Ident), *ColStr);
				Result = RegMatcher.FindNext();
			}
			Bools.Push(Result);

		} else if (Command.Type == Command_Op) {
			if (Command.Op == OP_And) {
				if (Bools.Num() > 1) {
					Bools[Bools.Num() - 2] &= Bools.Last();
					Bools.Pop();
				}

			} else if (Command.Op == OP_Or) {
				if (Bools.Num() > 1) {
					Bools[Bools.Num() - 2] |= Bools.Last();
					Bools.Pop();
				}

			} else if (Command.Op == OP_Not)
				Bools.Last() = !Bools.Last();
		}
	}

	if (Bools.Num())
		return Bools[0];
	else
		return false;
}

FAView SimpleSearchParser::Command::ToString() {
	if (Type == Command_Test) {
		FAView s = TMem.Printf("%d: %s", Tst.ColumnID, *Tst.Ident);
		if (Tst.Mod) {
			if      (Tst.Mod == Mod_Exact)        TMem.Append(&s, "[Exact]");
			else if (Tst.Mod == Mod_Regex)        TMem.Append(&s, "[Regex]");
			else if (Tst.Mod == Mod_Equal)        TMem.Append(&s, "[Equal]");
			else if (Tst.Mod == Mod_Greater)      TMem.Append(&s, "[Greater]");
			else if (Tst.Mod == Mod_Less)         TMem.Append(&s, "[Less]");
			else if (Tst.Mod == Mod_GreaterEqual) TMem.Append(&s, "[GreaterEqual]");
			else if (Tst.Mod == Mod_LessEqual)    TMem.Append(&s, "[LessEqual]");
		}
		return s;

	} else if (Type == Command_Op) {
		if      (Op == OP_And) return "AND";
		else if (Op == OP_Or)  return "OR";
		else if (Op == OP_Not) return "NOT";

	} else if (Type == Command_Store) return "STORE";

	return "";
}

// -------------------------------------------------------------------------------------------

struct InputTextCharCallbackUserData {
	TArray<char>& Str;
	ImGuiInputTextCallback ChainCallback;
	void* ChainCallbackUserData;
};

static int InputTextCharCallback(ImGuiInputTextCallbackData* data) {
	InputTextCharCallbackUserData* user_data = (InputTextCharCallbackUserData*)data->UserData;
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		// Resize string callback
		// If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
		TArray<char>& str = user_data->Str;

		str.SetNum(data->BufTextLen);
		if (str.Max() < data->BufSize + 1)
			str.Reserve(data->BufSize + 1);

		data->Buf = str.GetData();

	} else if (user_data->ChainCallback) {
		// Forward to user callback, if any
		data->UserData = user_data->ChainCallbackUserData;
		return user_data->ChainCallback(data);
	}
	return 0;
}

bool ImGuiAddon::InputText(const char* label, TArray<char>& str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
	flags |= ImGuiInputTextFlags_CallbackResize;

	InputTextCharCallbackUserData UserData = { str, callback, user_data };
	return ImGui::InputText(label, str.GetData(), str.Max() + 1, flags, InputTextCharCallback, &UserData);
}

bool ImGuiAddon::InputTextWithHint(const char* label, const char* hint, TArray<char>& str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data) {
	flags |= ImGuiInputTextFlags_CallbackResize;

	InputTextCharCallbackUserData UserData = { str, callback, user_data };
	return ImGui::InputTextWithHint(label, hint, str.GetData(), str.Max() + 1, flags, InputTextCharCallback, &UserData);
}

bool ImGuiAddon::InputString(FString Label, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags) {
	StringBuffer.Empty();
	StringBuffer.Append(ImGui_StoA(*String), String.Len() + 1);
	if (ImGuiAddon::InputText(ImGui_StoA(*Label), StringBuffer, flags)) {
		String = FString(StringBuffer);
		return true;
	}
	return false;
}

bool ImGuiAddon::InputStringWithHint(FString Label, FString Hint, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags) {
	StringBuffer.Empty();
	StringBuffer.Append(ImGui_StoA(*String), String.Len() + 1);
	if (ImGuiAddon::InputTextWithHint(ImGui_StoA(*Label), ImGui_StoA(*Hint), StringBuffer, flags)) {
		String = FString(StringBuffer);
		return true;
	}
	return false;
}

void ImGuiAddon::QuickTooltip(FString TooltipText, ImGuiHoveredFlags Flags) {
	if (ImGui::IsItemHovered(Flags)) {
		ImGui::BeginTooltip(); defer{ ImGui::EndTooltip(); };
		ImGui::Text(ImGui_StoA(*TooltipText));
	}
}

// -------------------------------------------------------------------------------------------

// @Todo: Improve.
const char* SearchBoxHelpText =
	"Multiple search terms can be entered.\n"
	"\n"
	"Operators are: \n"
	"	AND -> & or whitespace\n"
	"	OR  -> |\n"
	"	NOT -> !\n"
	"	\n"
	"	Example: \n"
	"		(a !b) | !(c & d)\n"
	"\n"
	"Modifiers for search terms are:\n"
	"	Exact -> +word\n"
	"	Regex -> regex: or reg: or r:\n"
	"	Value Comparisons -> =value, >value, <value, >=value, <=value\n"
	"\n"
	"Specify table column entries like this:\n"
	"	name:, value:, metadata:, type:, cpptype:, class:, category:, address:, size:\n"
	"\n"
	"	(name: is default, so the search term \"varName\" searches the property name column.)\n"
	"\n"
	"	Example: \n"
	"		intVar (value:>=3 | metadata:actor) size:>=10 size:<=100 cpptype:+int8\n"
	;

// @Todo: Improve.
const char* HelpText =
	"Drag an item somewhere to add it to the watch list.\n"
	"\n"
	"Shift click on a node -> Open/Close all.\n"
	"Shift click + digit on a node -> Specify how many layers to open.\n"
	"  Usefull since doing open all on an actor for example can open a whole lot of things.\n"
	"\n"
	"Right click on an item to inline it.\n"
	;

// -------------------------------------------------------------------------------------------

char* TempMemoryPool::MemBucket::Get(int Count) {
	char* Result = Data + Position;
	Position += Count;
	return Result;
}

void TempMemoryPool::Init(int _StartSize) {
	if (!IsInitialized) {
		*this = {};
		IsInitialized = true;
		StartSize = _StartSize;
	}

	if (Buckets.IsEmpty())
		AddBucket();
}

void TempMemoryPool::AddBucket() {
	int BucketSize = Buckets.Num() == 0 ? StartSize : Buckets.Last().Size * 2; // Double every bucket.
	MemBucket Bucket = {};
	Bucket.Data = (char*)FMemory::Malloc(BucketSize);
	Bucket.Size = BucketSize;
	Buckets.Add(Bucket);
}

void TempMemoryPool::ClearAll() {
	for (auto& It : Buckets)
		FMemory::Free(It.Data);
	Buckets.Empty();
	CurrentBucketIndex = 0;

	Markers.Empty();
}

void TempMemoryPool::GoToNextBucket() {
	if (!Buckets.IsValidIndex(CurrentBucketIndex + 1))
		AddBucket();
	CurrentBucketIndex++;
}

char* TempMemoryPool::Get(int Count) {
	while (!GetCurrentBucket().MemoryFits(Count))
		GoToNextBucket();

	return GetCurrentBucket().Get(Count);
}

void TempMemoryPool::Free(int Count) {
	int BytesToFree = Count;
	while (true) {
		auto Bucket = GetCurrentBucket();
		int BucketBytesToFree = FMath::Min(Bucket.Position, BytesToFree);
		Bucket.Free(BucketBytesToFree);
		BytesToFree -= BucketBytesToFree;
		if (BytesToFree)
			break;
		GoToPrevBucket();
	}
}

void TempMemoryPool::PushMarker() {
	Markers.Add({ CurrentBucketIndex, Buckets[CurrentBucketIndex].Position });
}

void TempMemoryPool::FreeToMarker() {
	auto& Marker = Markers.Last();
	while (CurrentBucketIndex != Marker.BucketIndex) {
		GetCurrentBucket().Position = 0;
		GoToPrevBucket();
	}
	GetCurrentBucket().Position = Marker.DataPosition;
}

void TempMemoryPool::PopMarker(bool Free) {
	if (Free)
		FreeToMarker();
	Markers.Pop(false);
}

// Copied from FString::Printf().
FAView TempMemoryPool::Printf(const char* Fmt, ...) {
	int BufferSize = 128;
	ANSICHAR* Buffer = 0;

	int ResultSize;
	while (true) {
		int Count = BufferSize * sizeof(ANSICHAR);
		Buffer = (ANSICHAR*)Get(Count);
		GET_VARARGS_RESULT_ANSI(Buffer, BufferSize, BufferSize - 1, Fmt, Fmt, ResultSize);

		if (ResultSize != -1)
			break;

		Free(Count);
		BufferSize *= 2;
	};

	Buffer[ResultSize] = '\0';

	FAView View(Buffer, ResultSize);
	return View;
}

// Copied partially from StringCast.
FAView TempMemoryPool::CToA(const TCHAR* SrcBuffer, int SrcLen) {
	int StringLength = TStringConvert<TCHAR, ANSICHAR>::ConvertedLength(SrcBuffer, SrcLen);
	int32 BufferSize = StringLength;
	ANSICHAR* Buffer = (ANSICHAR*)Get(BufferSize + 1);
	TStringConvert<TCHAR, ANSICHAR>::Convert(Buffer, BufferSize, SrcBuffer, SrcLen);
	Buffer[BufferSize] = '\0';

	FAView View(Buffer, BufferSize);
	return View;
}

// I wish there was a way to get the FName data ansi pointer directly instead of having to copy it.
FAView TempMemoryPool::NToA(FName Name) {
	const FNameEntry* NameEntry = Name.GetDisplayNameEntry();
	if (NameEntry->IsWide())
		return "<WideFNameError>";

	int Len = NameEntry->GetNameLength();
	ANSICHAR* Buffer = Get(Len + 1);
	NameEntry->GetAnsiName((ANSICHAR(&)[NAME_SIZE])(*Buffer));

	FAView View(Buffer, Len);
	return View;
}

} // namespace PropertyWatcher

#endif // UE_SERVER

#if defined(__clang__)
#pragma clang diagnostic pop
#endif