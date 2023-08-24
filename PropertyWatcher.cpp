/*
	Info:
		https://forums.unrealengine.com/t/a-sample-for-calling-a-ufunction-with-reflection/276416/9
		https://ikrima.dev/ue4guide/engine-programming/uobject-reflection/uobject-reflection/

		void UObject::ParseParms( const TCHAR* Parms )
		https://www.cnblogs.com/LynnVon/p/14794664.html

		https://github.com/ocornut/imgui/issues/718
		https://forums.unrealengine.com/t/using-clang-to-compile-on-windows/17478/51

	Not Important:
		- handle right clip copy and paste
		- export import everything with json
			try to use unreal serialization functions in properties.
		- favourites can be selected and deleted with del
		- selection happens on first cell background?
		- Sorting
		- make tool for stepping timesteps, pause, continue, maybe speed up gamespeed
		- idea: could filter everything if we do a complete full pre pass by asking all children if they have the searched item
		- Better line trace.
		- make save and restore functions for window settings
		- super bonus: hide things that have changed from base object like in details viewer in unreal
		- ability to modify contains like array and map, add/remove/insert and so on
		- look at EFieldIterationFlags
		- Metadata: use keys: Category, UIMin, UIMax
			maybe: tooltip, sliderexponent?
		- add ability to categorise any table column, right now we hardcoded class categories,
			but could make it dynamic and then use metadata categories
		- metadata categories, Property->MetaDataMap
		- add hot keys for enabling filter, classes, functions
		- call functions in watchItemPaths like Manager.SubObject.CallFunc:GetCurrentThing.Member
		- Make it easier to add unique struct handlings, make it so code is not so spread out.

		- unreal engine clang compile test

	Easy to implement:
		- add ctrl+shift+alt + numbers for menu navigation
		- ctrl+123 to switch between tabs

		- make value was updated animations
		- make background transparent mode, overlay style
		- if search input already focused, ctrl+f should select everything

	Things to handle:
		- WorldOutline, Actor components, widget tree

	Problems:
		- classes left navigation doesnt work
		- blueprint functions have wrong parameters in property cpp type
		- Delegates don't get inlined?

	global settings to change float text input to sliders, and also global settings for drag change speed
	crash on weakpointerget when bp selected camera search for private and disable classes

	***

	- print container preview up to property value region size, the more you open the column the more you see
	- Fix blueprint struct members that have _C_0 and so on
	- make mode that diffs current values of object with default class variables.

	- compare categories from widgets and actors and see how they are composed

	- for call functions dynamically we could use all our widgets for input and then store the result in a input
		we could then display it with drawItem, all the information is in the properties of the function, including the result property

	- draw widget tree

	- add option to auto push nodes on left side so you can see more and the padding isnt so big on big trees
	- clamp property name text to right and clip left so right side
		of text is always visible, which is more important

	- Button to clear custom storage like inlining
	- handle UberGaphFrame functions
	- Class pointer members maybe do default object.

	- column texts as value or string to distinguish between text and value filtering.

	***

	Todo before release:
		- search should be able to do structvar.membervar = 5
		- better search: maybe make a mode "hide non value types" that hides a list of things like delegates that you never want to see
			  maybe 2 search boxes, second for permanent stuff like hiding delegates, first one for quick searches.

		- search jump to next result
		- detached watch windows
		- custom draw functions for data
			- add right click "draw as" (rect, whatever)
			- manipulator widget based on data like position, show in world
		- simple function calling
			Make functions expandable in tree form, and leafs are arguments and return value
		- simple delegate calling
		- fix maps with touples
		- auto complete for watch window members
		- property path by member value
			- make path with custom getter-> instead of array index look at object and test for name or something
			- make custom path by dragging from a value, that should make link to parent node with that value being looked for

		- try imgui viewports

	***

	Server comm widget console explorer thing:
		- client ask server what vars there are, server answers.
		- if text box empty all names are listed
		- double click, when server answers list things

	imgui on server?
	maybe server could give answer for vars as json string
		like you ask him for stuff and he gets the var/struct and returns the thing as string

	maybe explorer for member vars
	Another idea: Make server send snapshot of object that you want to look at, server serializes it and client stores it

	allow overscroll in tables, so we dont jump up when we clamp

	make memory snapshot

	Bugs:
		- Error when inlining this PlayerController.AcknowledgedPawn.PlayerState.PawnPrivate.Controller.PlayerInput.DebugExecBindings in watch tab
			and setting inline stack to 2
			Structs don't inline correctly in watch window as first item.
*/

#if !UE_SERVER

#define PROPERTY_WATCHER_INTERNAL
#include "PropertyWatcher.h"
#undef PROPERTY_WATCHER_INTERNAL

#include "imgui.h"
#include "imgui_internal.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Engine/StaticMeshActor.h"

#include "GameFramework/PlayerController.h"

#include "UObject/Stack.h"
#include "Engine/Level.h"

#include "Misc/ExpressionParser.h"
#include "Internationalization/Regex.h"

//using namespace PropertyWatcher;

#if WITH_EDITORONLY_DATA
#define MetaData_Available true
#else 
#define MetaData_Available false
#endif

namespace PropertyWatcher {

void PropertyWatcher::Update(FString WindowName, TArray<PropertyItemCategory>& CategoryItems, TArray<MemberPath>& WatchedMembers, UWorld* World, bool* IsOpen, bool* WantsToSave, bool* WantsToLoad) {
	*WantsToSave = false;
	*WantsToLoad = false;
	
	// For convenience
	TArray<PropertyItem> Items;
	for (auto& It : CategoryItems)
		Items.Append(It.Items);

	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin(Ansi(*("Property Watcher: " + WindowName)), IsOpen, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }

	static ImVec2 FramePadding = ImVec2(ImGui::GetStyle().FramePadding.x, 2);

	if (ImGui::BeginMenuBar()) {
		defer{ ImGui::EndMenuBar(); };
		if (ImGui::BeginMenu("Settings")) {
			defer{ ImGui::EndMenu(); };

			ImGui::SetNextItemWidth(150);
			ImGui::DragFloat2("Item Padding", &FramePadding[0], 0.1f, 0.0, 20.0f, "%.0f");
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				FramePadding = ImVec2(ImGui::GetStyle().FramePadding.x, 2);
		}
		if (ImGui::BeginMenu("Help")) {
			defer{ ImGui::EndMenu(); };
			ImGui::Text(HelpText);
		}
	}

	// Top region.
	static bool ListFunctionsOnObjectItems = true;
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
			ImGui::Text(SearchBoxHelpText);
		}
		ImGui::SameLine();
		ImGui::Checkbox("Filter", &SearchFilterActive);
		ImGuiAddon::QuickTooltip("Enable filtering of rows that didn't pass the search in the search box.");
		ImGui::SameLine();
		ImGui::Checkbox("Classes", &EnableClassCategoriesOnObjectItems);
		ImGuiAddon::QuickTooltip("Enable sorting of actor member variables by classes with subsections.");
		ImGui::SameLine();
		ImGui::Checkbox("Functions", &ListFunctionsOnObjectItems);
		ImGuiAddon::QuickTooltip("Show functions in actor items.");
		ImGui::Spacing();

		static TArray<FString> ColumnNames = { "name", "value", "metadata", "type", "cpptype", "class", "category", "address", "size" };
		SearchParser.ParseExpression(SearchString, ColumnNames);

		// Debug show commands
		if (false)
		{
			int i = -1;
			for (auto It : SearchParser.Commands) {
				i++;
				if (i > 0) {
					ImGui::SameLine();
					ImGui::Text(",");
					ImGui::SameLine();
				}
				ImGui::Text(Ansi(*It.ToString()));
			}
		}
	}

	if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_None))
	{
		defer{ ImGui::EndTabBar(); };

		bool AddressHoveredThisFrame = false;
		static void* HoveredAddress = 0;
		static bool DrawHoveredAddresses = false;
		defer{ DrawHoveredAddresses = AddressHoveredThisFrame; };

		const char* Tabs[] = { "Objects", "Actors", "Watch" };
		for (int TabIndex = 0; TabIndex < IM_ARRAYSIZE(Tabs); TabIndex++)
		{
			FString CurrentTab = Tabs[TabIndex];

			static TArray<PropertyItem> ActorItems;
			static bool UpdateActorsEveryFrame = false;
			static bool SearchAroundPlayer = false;
			static float ActorsSearchRadius = 5;
			static bool DrawOverlapSphere = false;

			if (ImGui::BeginTabItem(Tabs[TabIndex]))
			{
				defer{ ImGui::EndTabItem(); };

				if (CurrentTab == "Watch") {
					if (ImGui::Button("Clear All"))
						WatchedMembers.Empty();
					ImGui::SameLine();
					if (ImGui::Button("Save"))
						*WantsToSave = true;
					ImGui::SameLine();
					if (ImGui::Button("Load"))
						*WantsToLoad = true;
				} else if (CurrentTab == "Actors") {
					static TArray<bool> CollisionChannelsActive;
					static bool InitActors = true;
					if (InitActors) {
						InitActors = false;
						int NumCollisionChannels = StaticEnum<ECollisionChannel>()->NumEnums();
						for (int i = 0; i < NumCollisionChannels; i++)
							CollisionChannelsActive.Push(false);
					}

					if (World->GetCurrentLevel()) {
						ImGui::Text("Current World: %s, ", Ansi(*World->GetName()));
						ImGui::SameLine();
						ImGui::Text("Current Level: %s", Ansi(*World->GetCurrentLevel()->GetName()));
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
								ImGui::Selectable(Ansi(*Name), &CollisionChannelsActive[i], ImGuiSelectableFlags_DontClosePopups);
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
				}

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, FramePadding); defer{ ImGui::PopStyleVar(); };

				ImVec2 TabItemSize = ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());
				ImGuiTableFlags TableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SortTristate;

				if (CurrentTab == "Actors")
					TableFlags |= ImGuiTableFlags_Sortable;

				if (ImGui::BeginTable("table", 10, TableFlags, TabItemSize)) {
					enum MyItemColumnID {
						ColumnID_PropertyName,
						ColumnID_PropertyCPPType,
						ColumnID_Address,
						ColumnID_Size,
					};

					int FlagSort = CurrentTab == "Actors" ? ImGuiTableColumnFlags_DefaultSort : ImGuiTableColumnFlags_NoSort;
					int FlagNoSort = ImGuiTableColumnFlags_NoSort;
					int FlagDefault = ImGuiTableColumnFlags_WidthStretch;

					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Property Name", FlagDefault | ImGuiTableColumnFlags_NoHide, 0.0, ColumnID_PropertyName);
					ImGui::TableSetupColumn("Property Value", FlagDefault | FlagNoSort);
					ImGui::TableSetupColumn("Metadata", FlagNoSort | ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("(?)").x);
					ImGui::TableSetupColumn("Property Type", FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide);
					ImGui::TableSetupColumn("CPP Type", FlagDefault, 0.0, ColumnID_PropertyCPPType);
					ImGui::TableSetupColumn("Owner Class", FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide);
					ImGui::TableSetupColumn("Category", FlagDefault | FlagNoSort | ImGuiTableColumnFlags_DefaultHide);
					ImGui::TableSetupColumn("Adress", FlagDefault | ImGuiTableColumnFlags_DefaultHide, 0.0, ColumnID_Address);
					ImGui::TableSetupColumn("Size", FlagDefault | ImGuiTableColumnFlags_DefaultHide, 0.0, ColumnID_Size);
					ImGui::TableSetupColumn("Remove", (CurrentTab == "Watch" ? 0 : ImGuiTableColumnFlags_Disabled) | ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
					ImGui::TableHeadersRow();

					TArray<FString> CurrentPath;
					TreeState State = {};
					State.SearchFilterActive = SearchFilterActive;
					State.DrawHoveredAddress = DrawHoveredAddresses;
					State.HoveredAddress = HoveredAddress;
					State.CurrentWatchItemIndex = -1;
					State.EnableClassCategoriesOnObjectItems = EnableClassCategoriesOnObjectItems;
					State.ListFunctionsOnObjectItems = ListFunctionsOnObjectItems;
					State.SearchParser = SearchParser;

					defer{
						if (State.AddressWasHovered) {
							State.AddressWasHovered = false;
							AddressHoveredThisFrame = true;
							HoveredAddress = State.HoveredAddress;
						};
					};

					if (CurrentTab == "Objects") { // @ObjectsTable
						for (auto& Category : CategoryItems) {
							bool MakeCategorySection = !Category.Name.IsEmpty();

							TreeNodeState NodeState = {};
							if (MakeCategorySection)
								BeginSection(Category.Name, NodeState, State, -1, ImGuiTreeNodeFlags_DefaultOpen);

							if (NodeState.IsOpen || !MakeCategorySection)
								for (auto& Item : Category.Items)
									DrawItemRow(State, Item, CurrentPath);

							if (MakeCategorySection)
								EndSection(NodeState, State);
						}

					} else if (CurrentTab == "Actors") { // @ActorsTable
						// Sorting.
						{
							static ImGuiTableSortSpecs* s_current_sort_specs = NULL;

							auto SortFun = [](const void* lhs, const void* rhs) -> int {
								PropertyItem* a = (PropertyItem*)lhs;
								PropertyItem* b = (PropertyItem*)rhs;
								for (int n = 0; n < s_current_sort_specs->SpecsCount; n++) {
									const ImGuiTableColumnSortSpecs* sort_spec = &s_current_sort_specs->Specs[n];
									int delta = 0;
									switch (sort_spec->ColumnUserID)
									{
									case ColumnID_PropertyName:    delta = (strcmp(Ansi(*((UObject*)a->Ptr)->GetName()), Ansi(*((UObject*)b->Ptr)->GetName()))); break;
									case ColumnID_PropertyCPPType: delta = (strcmp(Ansi(*a->GetCPPType()), Ansi(*b->GetCPPType()))); break;
									case ColumnID_Address:         delta = ((int64)a->Ptr - (int64)b->Ptr); break;
									case ColumnID_Size:            delta = (a->GetSize() - b->GetSize()); break;
									default: IM_ASSERT(0); break;
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

						for (auto& Item : ActorItems)
							DrawItemRow(State, Item, CurrentPath);

					} else if (CurrentTab == "Watch") { // @WatchTable
						int MemberIndexToDelete = -1;

						bool MoveHappened = false;
						int MoveIndexFrom = -1;
						int MoveIndexTo = -1;
						FString NewPathName;

						int i = 0;
						for (auto& Member : WatchedMembers) {
							State.CurrentWatchItemIndex = i++;
							State.WatchItemGotDeleted = false;
							State.RenameHappened = false;
							State.PathStringPtr = &Member.PathString;

							bool Found = Member.UpdateItemFromPath(Items);
							if (!Found) {
								Member.CachedItem.Ptr = 0;
								Member.CachedItem.Prop = 0;
							}

							DrawItemRow(State, Member.CachedItem, CurrentPath);

							if (State.WatchItemGotDeleted)
								MemberIndexToDelete = State.CurrentWatchItemIndex;

							if (State.MoveHappened) {
								MoveHappened = true;
								MoveIndexFrom = State.MoveFrom;
								MoveIndexTo = State.MoveTo;
							}
						}

						if (MemberIndexToDelete != -1)
							WatchedMembers.RemoveAt(MemberIndexToDelete);

						if (MoveHappened)
							WatchedMembers.Swap(MoveIndexFrom, MoveIndexTo);
					}

					ImGui::EndTable();
					ImGui::Text("Item count: %d", State.ItemDrawCount);
				}
			}
		}
	}

	ImRect TargetRect(ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax());
	TargetRect.Translate(ImGui::GetWindowPos());
	TargetRect.Translate(ImVec2(0, ImGui::GetScrollY()));

	if (ImGui::BeginDragDropTargetCustom(TargetRect, ImGui::GetHoveredID())) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PropertyWatcherMember")) {
			char* Payload = (char*)payload->Data;

			MemberPath Member;
			Member.PathString = Payload;
			WatchedMembers.Push(Member);
		}
	}
}

void PropertyWatcher::DrawItemRow(TreeState& State, PropertyItem Item, TArray<FString>& CurrentMemberPath, int StackIndex) {
	if (State.ItemDrawCount > 100000) // @Todo: Random safety measure against infinite recursion, do better.
		return;

	TMap<FName, FString> ColumnTexts;
	bool ItemIsSearched;
	{
		ColumnTexts.Add("name", GetColumnCellText(State, Item, "name", &CurrentMemberPath, &StackIndex)); // Default.

		// Cache the cell texts that we need for the text search.
		for (auto Command : State.SearchParser.Commands)
			if (Command.Type == SimpleSearchParser::Command_Test && !ColumnTexts.Find(Command.Tst.Column))
				ColumnTexts.Add(Command.Tst.Column, GetColumnCellText(State, Item, Command.Tst.Column, &CurrentMemberPath, &StackIndex));

		ItemIsSearched = State.SearchParser.ApplyTests(ColumnTexts);
	}

	auto FindOrGetColumnText = [&State, &Item, &ColumnTexts, &CurrentMemberPath, &StackIndex](FName ColumnName) -> FString {
		if (const FString* Result = ColumnTexts.Find(ColumnName))
			return *Result;
		else
			return GetColumnCellText(State, Item, ColumnName);
	};

	// Item is skipped.
	if (State.SearchFilterActive && !ItemIsSearched && !Item.CanBeOpened())
		return;

	// Misc setup.

	State.ItemDrawCount++;
	CurrentMemberPath.Push(Item.GetDisplayName());

	if (StackIndex == 0) {
		if (!Item.NameIDOverwrite.IsEmpty())
			// Usefull if you want to add an object to the object table, that 
			// has an unstable pointer and name. Not used anywhere else yet.
			ImGui::PushID(Ansi(*Item.NameIDOverwrite));
		else
			ImGui::PushID(Item.Ptr);
	} else
		ImGui::PushID(Ansi(*Item.GetDisplayName()));

	TreeNodeState NodeState;

	// Executed after all members are drawn.
	defer{
		CurrentMemberPath.Pop();
		ImGui::PopID();
		EndTreeNode(NodeState, State);
	};

	// @Column(name): Property name
	{
		if (!Item.IsValid()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		defer{ if (!Item.IsValid()) ImGui::PopStyleColor(1); };

		{
			NodeState = {};
			NodeState.HasBranches = Item.CanBeOpened();
			NodeState.ItemInfo = VisitedPropertyInfo::FromItem(Item);

			if (ItemIsSearched) {
				NodeState.PushTextColor = true;
				NodeState.TextColor = ImVec4(1, 0.5f, 0, 1);
			}

			BeginTreeNode("##Object", FindOrGetColumnText("name"), NodeState, State, StackIndex, 0);
		}

		bool NodeIsMarkedAsInlined = false;

		// Right click popup for inlining.
		{
			// Do tree push to get right bool from storage when node is open or closed.
			if (!NodeState.IsOpen) { ImGui::TreePush("##Object"); };
			defer{ if (!NodeState.IsOpen) { ImGui::TreePop(); } };

			auto StorageIDIsInlined = ImGui::GetID("IsInlined");
			auto StorageIDInlinedStackDepth = ImGui::GetID("InlinedStackDepth");

			auto Storage = ImGui::GetStateStorage();
			NodeIsMarkedAsInlined = Storage->GetBool(StorageIDIsInlined);
			int InlinedStackDepth = Storage->GetInt(StorageIDInlinedStackDepth, 0);

			TreeNodeSetInline(NodeState, State, CurrentMemberPath.Num(), StackIndex, NodeIsMarkedAsInlined, InlinedStackDepth);

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
		}

		// Drag to watch window.
		if (StackIndex > 0 && Item.Type != PointerType::Function) {
			if (ImGui::BeginDragDropSource()) {
				FString PathString = FString::Join(CurrentMemberPath, TEXT("."));
				const char* String = Ansi(*PathString);
				ImGui::SetDragDropPayload("PropertyWatcherMember", String, PathString.Len() + 1);

				ImGui::Text("Add to watch list:");
				ImGui::Text(String);
				ImGui::EndDragDropSource();
			}
		}

		// Drag to move watch item. Only available when top level watch list item.
		if (State.CurrentWatchItemIndex != -1 && StackIndex == 0) {
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
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); defer{ ImGui::PopStyleColor(1); };
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());

				FString StringID = FString::Printf(TEXT("##InputPathText %d"), State.CurrentWatchItemIndex);

				static TArray<char> StringBuffer;
				StringBuffer.Empty();
				StringBuffer.Append(Ansi(**State.PathStringPtr), State.PathStringPtr->Len() + 1);
				if (ImGuiAddon::InputText(Ansi(*StringID), StringBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
					(*State.PathStringPtr) = FString(StringBuffer);
			}
		}

		if (NodeIsMarkedAsInlined) {
			ImGui::SameLine();
			ImGui::Text("*");
		}
	}

	// Draw other columns if visible. 
	// @Todo: Should check row and not tree node for visibility.
	if (!ImGui::IsItemVisible()) {
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
			FString MetaDataText = FindOrGetColumnText("metadata");
			if (MetaDataText.Len()) {
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);

					if (MetaData_Available)
						ImGui::TextUnformatted(Ansi(*MetaDataText));
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
					ImGui::PushStyleColor(ImGuiCol_Text, cText); defer{ ImGui::PopStyleColor(); };
					ImGui::Bullet();
				}
				ImGui::SameLine();
				ImGui::Text(Ansi(*FindOrGetColumnText("type")));
			}
		}

		// @Column(cpptype): CPP Type
		if (ImGui::TableNextColumn())
			ImGui::Text(Ansi(*FindOrGetColumnText("cpptype")));

		// @Column(class): Owner Class
		if (ImGui::TableNextColumn())
			ImGui::Text(Ansi(*FindOrGetColumnText("class")));

		// @Column(category): Metadata Category
		if (ImGui::TableNextColumn())
			ImGui::Text(Ansi(*FindOrGetColumnText("category")));

		// @Column(address): Adress
		if (ImGui::TableNextColumn()) {
			if (State.DrawHoveredAddress && Item.Ptr == State.HoveredAddress)
				ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), Ansi(*FindOrGetColumnText("address")));
			else
				ImGui::Text(Ansi(*FindOrGetColumnText("address")));

			if (ImGui::IsItemHovered()) {
				State.AddressWasHovered = true;
				State.HoveredAddress = Item.Ptr;
			}
		}

		// @Column(size): Size
		if (ImGui::TableNextColumn())
			ImGui::Text(Ansi(*FindOrGetColumnText("size")));

		// Close Button
		if (ImGui::TableNextColumn())
			if (State.CurrentWatchItemIndex != -1 && StackIndex == 0)
				if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0)))
					State.WatchItemGotDeleted = true;
	}

	// Draw leaf properties.
	if (NodeState.IsOpen) {
		bool PushAddressesStack = State.ForceToggleNodeOpenClose || State.ForceInlineChildItems;
		if (PushAddressesStack)
			State.VisitedPropertiesStack.Push(VisitedPropertyInfo::FromItem(Item));

		DrawItemChildren(State, Item, CurrentMemberPath, StackIndex);

		if (PushAddressesStack)
			State.VisitedPropertiesStack.Pop();
	}
}

void PropertyWatcher::DrawItemChildren(TreeState& State, PropertyItem Item, TArray<FString>& CurrentMemberPath, int StackIndex) {
	check(Item.Ptr); // Do we need this check here? Can't remember.

	if (Item.Prop &&
		(Item.Prop->IsA(FWeakObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FLazyObjectProperty::StaticClass()) ||
			Item.Prop->IsA(FSoftObjectProperty::StaticClass()))) {
		UObject* Obj = 0;
		bool IsValid = GetObjFromObjPointerProp(Item, Obj);
		if (IsValid) {
			auto NewItem = MakeObjectItem(Obj);
			return DrawItemChildren(State, NewItem, CurrentMemberPath, StackIndex + 1);
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
				SectionHelper.Names.Push(((FField*)(Member).Prop)->Owner.GetName());

			SectionHelper.Init();
		}

		if (!SectionHelper.Enabled) {
			for (auto It : Members)
				DrawItemRow(State, It, CurrentMemberPath, StackIndex + 1);

		} else {
			for (int SectionIndex = 0; SectionIndex < SectionHelper.GetSectionCount(); SectionIndex++) {
				int MemberStartIndex, MemberEndIndex;
				FString CurrentSectionName = SectionHelper.GetSectionInfo(SectionIndex, MemberStartIndex, MemberEndIndex);

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
			defer{ EndSection(FunctionSection, State); };

			if (FunctionSection.IsOpen) {
				SectionHelper SectionHelper;
				if (State.EnableClassCategoriesOnObjectItems) {
					for (auto& Function : Functions)
						SectionHelper.Names.Push(Function->GetOuterUClass()->GetName());

					SectionHelper.Init();
				}

				if (!SectionHelper.Enabled) {
					for (auto It : Functions)
						DrawItemRow(State, MakeFunctionItem(Item.Ptr, It), CurrentMemberPath, StackIndex + 1);

				} else {
					for (int SectionIndex = 0; SectionIndex < SectionHelper.GetSectionCount(); SectionIndex++) {
						int MemberStartIndex, MemberEndIndex;
						FString CurrentSectionName = SectionHelper.GetSectionInfo(SectionIndex, MemberStartIndex, MemberEndIndex);

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
		}
	}
}

FString PropertyWatcher::GetColumnCellText(TreeState& State, PropertyItem& Item, FName ColumnName, TArray<FString>* CurrentMemberPath, int* StackIndex) {
	FString Result = "";
	if (ColumnName == "name") {
		if (CurrentMemberPath && StackIndex) {
			bool TopLevelWatchListItem = State.CurrentWatchItemIndex != -1 && (*StackIndex) == 0;
			bool PathIsEditable = TopLevelWatchListItem && State.PathStringPtr;

			if (!PathIsEditable) {
				FString StrName = Item.GetDisplayName();

				if (State.ForceInlineChildItems) {
					TArray<FString> InlinedMemberPath = *CurrentMemberPath;
					for (int i = 0; i < State.InlineMemberPathIndexOffset; i++) {
						if (InlinedMemberPath.Num())
							InlinedMemberPath.RemoveAt(0);
					}
					InlinedMemberPath.Push(StrName);
					Result = FString::Join(InlinedMemberPath, TEXT("."));
				} else
					Result = StrName;
			}
		}

	} else if (ColumnName == "value") {
		Result = GetValueStringFromItem(Item);

	} else if (ColumnName == "metadata" && Item.Prop) {
#if MetaData_Available
		if (const TMap<FName, FString>* MetaData = Item.Prop->GetMetaDataMap()) {
			TArray<FName> Keys;
			MetaData->GenerateKeyArray(Keys);
			int i = 0;
			for (auto Key : Keys) {
				if (i != 0) Result.Append("\n\n");
				i++;

				const FString* Value = MetaData->Find(Key);
				Result.Append(FString::Printf(TEXT("%s:\n\t"), *Key.ToString()));
				Result.Append(*Value);
			}
		}
#endif

	} else if (ColumnName == "type") {
		Result = Item.GetPropertyType();

	} else if (ColumnName == "cpptype") {
		Result = Item.GetCPPType();

	} else if (ColumnName == "class") {
		if (Item.Prop) {
			FFieldVariant Owner = ((FField*)Item.Prop)->Owner;
			Result = Owner.GetName();

		} else if (Item.Type == PointerType::Function) {
			UClass* Class = ((UFunction*)Item.StructPtr)->GetOuterUClass();
			if (Class)
				Result = Class->GetName();
		}

	} else if (ColumnName == "category") {
		Result = GetItemMetadataCategory(Item);

	} else if (ColumnName == "address") {
		Result = FString::Printf(TEXT("%p"), Item.Ptr);

	} else if (ColumnName == "size") {
		int Size = Item.GetSize();
		if (Size != -1)
			Result = FString::Printf(TEXT("%d B"), Size);
	}

	return Result;
}

FString PropertyWatcher::GetValueStringFromItem(PropertyItem& Item) {
	// Maybe we could just serialize the property to string?
	// Since we don't handle that many types for now we can just do it by hand.
	FString Result;

	if (!Item.Ptr)
		Result = "Null";

	else if (!Item.Prop)
		Result = "";

	else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Item.Prop))
		Result = NumericProp->GetNumericPropertyValueToString(Item.Ptr);

	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Item.Prop))
		Result = ((bool*)(Item.Ptr)) ? "true" : "false";

	else if (Item.Prop->IsA(FStrProperty::StaticClass()))
		Result = *(FString*)Item.Ptr;

	else if (Item.Prop->IsA(FNameProperty::StaticClass()))
		Result = ((FName*)Item.Ptr)->ToString();

	else if (Item.Prop->IsA(FTextProperty::StaticClass()))
		Result = ((FText*)Item.Ptr)->ToString();

	return Result;
}

void PropertyWatcher::DrawPropertyValue(PropertyItem& Item) {
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
		ImGui::Text(Ansi(*Class->GetAuthoredName()));

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
				StringBuffer.Append(Ansi(*Name), Name.Len());
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
		ImGui::Text("%s [%d]", Ansi(*CurrentProp->GetCPPType()), ScriptArrayHelper.Num());

	} else if (FMapProperty* MapProp = CastField<FMapProperty>(Item.Prop)) {
		FScriptMapHelper Helper = FScriptMapHelper(MapProp, Item.Ptr);
		ImGui::Text("<%s, %s> (%d)", Ansi(*MapProp->KeyProp->GetCPPType()), Ansi(*MapProp->ValueProp->GetCPPType()), Helper.Num());

	} else if (FSetProperty* SetProp = CastField<FSetProperty>(Item.Prop)) {
		FScriptSetHelper Helper = FScriptSetHelper(SetProp, Item.Ptr);
		ImGui::Text("<%s> {%d}", Ansi(*Helper.GetElementProperty()->GetCPPType()), Helper.Num());

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
		ImGui::Text(Ansi(*Text));

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

bool PropertyWatcher::MemberPath::UpdateItemFromPath(TArray<PropertyItem>& Items) {
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
			if (It.GetDisplayName() == MemberArray[0]) {
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
				FString ItemName = MemberItem.GetDisplayName();

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
	CachedItem.NameOverwrite = PathString;

	return !SearchFailed;
}

FString PropertyWatcher::ConvertWatchedMembersToString(TArray<MemberPath>& WatchedMembers) {
	TArray<FString> Strings;
	for (auto It : WatchedMembers)
		Strings.Push(It.PathString);

	FString Result = FString::Join(Strings, TEXT(","));
	return Result;
}

void PropertyWatcher::LoadWatchedMembersFromString(FString Data, TArray<MemberPath>& WatchedMembers) {
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

FString PropertyItem::GetName() {
	if (Type == PointerType::Property && Prop)
		return Prop->GetName();

	if (Type == PointerType::Object && Ptr)
		return ((UObject*)Ptr)->GetName();

	if (StructPtr && (Type == PointerType::Struct || Type == PointerType::Function))
		return StructPtr->GetAuthoredName();

	return "";
}

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

		bool Inlined;
		{
			FString Extended;
			FString StructType = StructProp->GetCPPType(&Extended, 0);
			// @Todo: These shouldn't be hardcoded.
			static TArray<FString> Types = { "FVector", "FRotator", "FVector2D", "FIntVector2", "FIntVector", "FTimespan", "FDateTime" };
			Inlined = Types.Contains(StructType);
		}
		return !Inlined;
	}

	if (Prop->IsA(FDelegateProperty::StaticClass()))
		return true;

	if (Prop->IsA(FMulticastDelegateProperty::StaticClass()))
		return true;

	return false;
}

FString PropertyItem::GetPropertyType() {
	FString Result = "";
	if (Type == PointerType::Property && Prop)
		Result = Prop->GetClass()->GetName();

	else if (Type == PointerType::Object)
		Result = "";

	else if (Type == PointerType::Struct)
		Result = "";

	return Result;
};

FString PropertyItem::GetCPPType() {
	if (Type == PointerType::Property && Prop) return Prop->GetCPPType();
	if (Type == PointerType::Struct)           return ((UScriptStruct*)StructPtr)->GetStructCPPName();
	if (Type == PointerType::Object) {
		UClass* Class = ((UObject*)Ptr)->GetClass();
		if (Class) return Class->GetName();
	}

	// Do we really have to do this? Is there no engine function?
	if (Type == PointerType::Function) {
		UFunction* Function = (UFunction*)StructPtr;

		FProperty* ReturnProp = Function->GetReturnProperty();
		FString ts = ReturnProp ? ReturnProp->GetCPPType() : "void";

		ts += " (";
		int i = 0;
		for (FProperty* MemberProp : TFieldRange<FProperty>(Function)) {
			if (MemberProp == ReturnProp) continue;
			if (i == 1) ts += ", ";
			ts += MemberProp->GetCPPType();
			i++;
		}
		ts += ")";

		return ts;
	}

	return "";
};

int PropertyItem::GetSize() {
	if (Prop) return Prop->GetSize();
	else if (Type == PointerType::Object) {
		UClass* Class = ((UObject*)Ptr)->GetClass();
		if (!Class) return Class->GetPropertiesSize();
	} else if (Type == PointerType::Struct) return StructPtr->GetPropertiesSize();
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

			MemberArray->Push(MakePropertyItemNamed(KeyPtr, KeyProp, FString::Printf(TEXT("[%d] Key"), i)));
			MemberArray->Push(MakePropertyItemNamed(ValuePtr2, ValueProp, FString::Printf(TEXT("[%d] Value"), i)));
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

void* PropertyWatcher::ContainerToValuePointer(PointerType Type, void* ContainerPtr, FProperty* MemberProp) {
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

PropertyItem PropertyWatcher::MakeObjectItem(void* _Ptr) {
	return { PointerType::Object, _Ptr };
}
PropertyItem PropertyWatcher::MakeObjectItemNamed(void* _Ptr, FString _NameOverwrite, FString NameID) {
	return { PointerType::Object, _Ptr, 0, _NameOverwrite, 0, NameID };
}
PropertyItem PropertyWatcher::MakeArrayItem(void* _Ptr, FProperty* _Prop, int _Index) {
	return { PointerType::Property, _Ptr, _Prop, FString::Printf(TEXT("[%d]"), _Index) };
}
PropertyItem PropertyWatcher::MakePropertyItem(void* _Ptr, FProperty* _Prop) {
	return { PointerType::Property, _Ptr, _Prop };
}
PropertyItem PropertyWatcher::MakePropertyItemNamed(void* _Ptr, FProperty* _Prop, FString Name, FString NameID) {
	return { PointerType::Property, _Ptr, _Prop, Name, 0, NameID };
}
PropertyItem PropertyWatcher::MakeFunctionItem(void* _Ptr, UFunction* _Function) {
	return { PointerType::Function, _Ptr, 0, "", _Function };
}

// -------------------------------------------------------------------------------------------

void PropertyWatcher::SetTableRowBackgroundByStackIndex(int StackIndex) {
	if (StackIndex == 0)
		return;

	ImVec4 c = { 0, 0, 0, 0.05f };
	float h = ((StackIndex - 1) % 4) / 4.0f + 0.065; // Start with orange, cycle 4 colors.
	ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, c.x, c.y, c.z);
	ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(c));
}

int GetDigitKeyDownAsInt() {
	for (int i = 0; i < 10; i++) {
		ImGuiKey DigitKeyCode = (ImGuiKey)(ImGuiKey_0 + i);
		if (ImGui::IsKeyDown(DigitKeyCode))
			return i;
	}
	return 0;
}

bool PropertyWatcher::BeginTreeNode(FString NameID, FString DisplayName, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags) {
	bool ItemIsInlined = State.ForceInlineChildItems && (StackIndex < State.InlineStackIndexLimit) && NodeState.HasBranches;

	if (State.ForceInlineChildItems && State.VisitedPropertiesStack.ContainsByPredicate([&NodeState](auto It) { return It.Compare(NodeState.ItemInfo); }))
		ItemIsInlined = false;

	if (ItemIsInlined) {
		NodeState.IsOpen = true;
		ImGui::TreePush(Ansi(*NameID));
		ImGui::Unindent();
		NodeState.ItemIsInlined = true;

	} else {
		// Start new row. Column index before this should be LastColumnIndex + 1.
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();

		SetTableRowBackgroundByStackIndex(NodeState.VisualStackIndex != -1 ? NodeState.VisualStackIndex : StackIndex);

		if (NodeState.PushTextColor)
			ImGui::PushStyleColor(ImGuiCol_Text, NodeState.TextColor);
		defer{
			if (NodeState.PushTextColor)
				ImGui::PopStyleColor(1);
		};

		bool NodeStateChanged = false;

		// If force open mode is active we change the state of the node if needed.
		if (State.ForceToggleNodeOpenClose && NodeState.HasBranches) {
			auto StateStorage = ImGui::GetStateStorage();
			auto ID = ImGui::GetID(Ansi(*NameID));
			bool IsOpen = (bool)StateStorage->GetInt(ID);

			// Tree node state should change.
			if (State.ForceToggleNodeMode != IsOpen) {
				bool StateChangeAllowed = true;

				// Checks when trying to toggle open node.
				if (State.ForceToggleNodeMode) {
					// Address already visited.
					if (NodeState.ItemInfo.Address && State.VisitedPropertiesStack.ContainsByPredicate([&NodeState](auto It) { return It.Compare(NodeState.ItemInfo); }))
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

		int Flags = ExtraFlags;
		{
			Flags |= NodeState.HasBranches ? ImGuiTreeNodeFlags_NavLeftJumpsBackHere : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (NodeState.OverrideNoTreePush)
				Flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen;

			bool Open = ImGui::TreeNodeEx(Ansi(*NameID), Flags, Ansi(*DisplayName));
			if (NodeState.HasBranches)
				NodeState.IsOpen = Open;
		}

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
				ImGui::TreePush(Ansi(*NameID));

			NodeState.IsOpen = true;
		}
	}

	return NodeState.IsOpen;
}

void PropertyWatcher::TreeNodeSetInline(TreeNodeState& NodeState, TreeState& State, int CurrentMemberPathLength, int StackIndex, bool Inline, int InlineStackDepth) {
	if (Inline && !State.ForceInlineChildItems) {
		NodeState.InlineChildren = true;

		State.ForceInlineChildItems = true;
		State.InlineStackIndexLimit = InlineStackDepth == 0 ? StackIndex + 100 : StackIndex + InlineStackDepth;

		State.InlineMemberPathIndexOffset = CurrentMemberPathLength;
		State.VisitedPropertiesStack.Empty();
	}
}

void PropertyWatcher::EndTreeNode(TreeNodeState& NodeState, TreeState& State) {
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

bool PropertyWatcher::BeginSection(FString Name, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags) {
	bool NodeOpenCloseLogicIsEnabled = true;

	NodeState.HasBranches = true;
	NodeState.PushTextColor = true;
	NodeState.TextColor = ImVec4(1, 1, 1, 0.5f);
	NodeState.VisualStackIndex = StackIndex + 1;

	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0)); defer{ ImGui::PopStyleColor(1); };

	const char* CharName = Ansi(*("(" + Name + ")"));
	ExtraFlags |= ImGuiTreeNodeFlags_Framed;
	bool IsOpen = BeginTreeNode(CharName, CharName, NodeState, State, StackIndex, ExtraFlags);

	// Nothing else is drawn in the row so we skip to the next one.
	ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);

	return IsOpen;
}

void PropertyWatcher::EndSection(TreeNodeState& NodeState, TreeState& State) {
	EndTreeNode(NodeState, State);
}

TArray<FName> PropertyWatcher::GetClassFunctionList(UClass* Class) {
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

TArray<UFunction*> PropertyWatcher::GetObjectFunctionList(UObject* Obj) {
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

FString PropertyWatcher::GetItemMetadataCategory(PropertyItem& Item) {
	FString Category = "";
#if WITH_EDITORONLY_DATA											
	if (Item.Prop) {
		if (const TMap<FName, FString>* MetaData = Item.Prop->GetMetaDataMap()) {
			if (const FString* Value = MetaData->Find("Category"))
				Category = *Value;
		}
	}
#endif
	return Category;
}

bool PropertyWatcher::GetItemColor(PropertyItem& Item, ImVec4& Color) {
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

bool PropertyWatcher::GetObjFromObjPointerProp(PropertyItem& Item, UObject*& Object) {
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

void SimpleSearchParser::ParseExpression(FString str, TArray<FString> _Columns) {
	Commands.Empty();

	struct StackInfo {
		TArray<Test> Tests = { {} };
		TArray<Operator> OPs;
	};
	TArray<StackInfo> Stack = { {} };

	auto EatToken = [&str](FString Token) -> bool {
		if (str.StartsWith(Token)) {
			str.RemoveAt(0, Token.Len());
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

		if      (EatToken("|"))   Stack.Last().OPs.Push(OP_Or);
		else if (EatToken("!"))   Stack.Last().OPs.Push(OP_Not);
		else if (EatToken("+"))   Stack.Last().Tests.Last().Mod = Mod_Exact;
		else if (EatToken("r:"))  Stack.Last().Tests.Last().Mod = Mod_Regex;
		else if (EatToken("<="))  Stack.Last().Tests.Last().Mod = Mod_LessEqual;
		else if (EatToken(">="))  Stack.Last().Tests.Last().Mod = Mod_GreaterEqual;
		else if (EatToken("<"))   Stack.Last().Tests.Last().Mod = Mod_Less;
		else if (EatToken(">"))   Stack.Last().Tests.Last().Mod = Mod_Greater;
		else if (EatToken("="))   Stack.Last().Tests.Last().Mod = Mod_Equal;
		else if (EatToken("("))   Stack.Push({});

		else if (EatToken(")")) {
			Stack.Pop();
			if (!Stack.Num()) break;
			PushedWord();

		} else {
			// Column Name
			{
				bool Found = false;
				for (int i = 0; i < _Columns.Num(); i++) {
					if (str.StartsWith(_Columns[i] + ':')) {
						Stack.Last().Tests.Last().Column = FName(_Columns[i]);
						str.RemoveAt(0, _Columns[i].Len());
						Found = true;
						break;
					}
				}
				if (Found) continue;
			}

			if (EatToken("\"")) {
				int Index;
				if (str.FindChar('"', Index)) {
					Stack.Last().Tests.Last().Ident = str.Left(Index);
					str.RemoveAt(0, Index + 1);

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
					str.RemoveAt(0);
					continue;
				}

				Stack.Last().Tests.Last().Ident = str.Left(Index);
				str.RemoveAt(0, Index);

				Commands.Push({ Command_Test, Stack.Last().Tests.Last() });
				PushedWord();
			}
		}
	}

	for (auto& It : Commands)
		if (It.Type == Command_Test && It.Tst.Column == NAME_None)
			It.Tst.Column = "name";
}

bool SimpleSearchParser::ApplyTests(TMap<FName, FString>& ColumnTexts) {
	TArray<bool> Bools;
	for (auto Command : Commands) {
		if (Command.Type == Command_Test) {
			Test& Tst = Command.Tst;
			FString* ColStr = ColumnTexts.Find(Tst.Column);
			if (!ColStr)
				continue;

			bool Result;
			if (!Tst.Mod)                     Result = ColStr->Contains(Tst.Ident);
			else if (Tst.Mod == Mod_Exact)        Result = ColStr->Equals(Tst.Ident, ESearchCase::IgnoreCase);
			else if (Tst.Mod == Mod_Equal)        Result = FCString::Atod(**ColStr) == FCString::Atod(*Tst.Ident);
			else if (Tst.Mod == Mod_Greater)      Result = FCString::Atod(**ColStr) > FCString::Atod(*Tst.Ident);
			else if (Tst.Mod == Mod_Less)         Result = FCString::Atod(**ColStr) < FCString::Atod(*Tst.Ident);
			else if (Tst.Mod == Mod_GreaterEqual) Result = FCString::Atod(**ColStr) >= FCString::Atod(*Tst.Ident);
			else if (Tst.Mod == Mod_LessEqual)    Result = FCString::Atod(**ColStr) <= FCString::Atod(*Tst.Ident);

			else if (Tst.Mod == Mod_Regex) {
				FRegexMatcher RegMatcher(FRegexPattern(Tst.Ident), *ColStr);
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
		ImGui::Text(Ansi(*TooltipText));
	}
}

// -------------------------------------------------------------------------------------------

// @Todo: Improve.
const char* PropertyWatcher::SearchBoxHelpText =
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
const char* PropertyWatcher::HelpText =
	"Drag an item somewhere to add it to the watch list.\n"
	"\n"
	"Shift click on a node -> Open/Close all.\n"
	"Shift click + digit on a node -> Specify how many layers to open.\n"
	"  Usefull since doing open all on an actor for example can open a whole lot of things.\n"
	"\n"
	"Right click on an item to inline it.\n"
	;

}

#endif
