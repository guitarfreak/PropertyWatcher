/*
	PropertyWatcher - v0.3.1 - http://github.com/guitarfreak/PropertyWatcher
	by Roy Thieme

	INFO:
		A runtime variable watch window for Unreal Engine using ImGui.
		
	DEPENDENCIES:
		Needs ImGui to work. Search github for an Unreal ImGui backend plugin.

	USAGE EXAMPLE:
		...
	
		#include "Misc/FileHelper.h"
		#include "Kismet/GameplayStatics.h"
	
		...
	
		static bool PropertyWatcherIsOpen = true;
		if (PropertyWatcherIsOpen) {
			static bool PropertyWatcherInit = true;
			static TArray<PropertyWatcher::MemberPath> WatchedMembers;

			auto World = GetWorld();
			auto GameInstance = GetGameInstance();
			auto GameMode = UGameplayStatics::GetGameMode(GetWorld());
			auto PlayerController = World->GetFirstPlayerController();

			PropertyWatcher::PropertyItemCategory CatA = { "Group A", {
				PropertyWatcher::MakeObjectItem(GetWorld()),
				PropertyWatcher::MakeObjectItemNamed(GameInstance, GameInstance->GetClass()->GetName()),

				// You can also add structs like this:
				// PropertyWatcherMakeStructItem(FMyStruct, &MyStruct),
				//
				// That said, the function macro is using StaticStruct<StructType>() to get the script struct, but I
				// couldn't get it to work for built in structs while writing this usage code.
				// But local self made structs should definitely work.
			} };

			PropertyWatcher::PropertyItemCategory CatB = { "Group B", {
				PropertyWatcher::MakeObjectItem(GameMode),
				PropertyWatcher::MakeObjectItemNamed(PlayerController, PlayerController->GetClass()->GetName()),
			} };

			TArray<PropertyWatcher::PropertyItemCategory> Objects = { CatA, CatB };

			bool WantsToSave, WantsToLoad;
			PropertyWatcher::Update("Actors", Objects, WatchedMembers, GetWorld(), &PropertyWatcherIsOpen, &WantsToSave, &WantsToLoad);

			if (PropertyWatcherInit)
				WantsToLoad = true;

			FString WatchWindowFilePath = FPaths::ProjectSavedDir() + "ImGui/PropertyWatcher-WatchWindowMembers.txt";
			if (WantsToSave)
				FFileHelper::SaveStringToFile(PropertyWatcher::ConvertWatchedMembersToString(WatchedMembers), *WatchWindowFilePath);

			if (WantsToLoad) {
				FString Data;
				if (FFileHelper::LoadFileToString(Data, *WatchWindowFilePath))
					PropertyWatcher::LoadWatchedMembersFromString(Data, WatchedMembers);
			}

			PropertyWatcherInit = false;
		}

		...

	LICENSE:
		See end of file for license information.
*/

#pragma once

#if !UE_SERVER
#include "imgui.h"

namespace PropertyWatcher {
	enum PointerType {
		Property = 0,
		Object,
		Struct,
		Array,
		Map,
		Function,
	};

	struct PropertyItem {
		// Maybe in future write list of all possible combinations that are possible and or used in code.
		// So we don't have to check everything all the time.

		PointerType Type = PointerType::Property;
		void* Ptr = 0;
		FProperty* Prop = 0;
		FString NameOverwrite = "";
		UStruct* StructPtr = 0; // Top level structs use this as UScriptStruct and Functions as UFunction
		FString NameIDOverwrite = ""; // Optional

		// Awkward, but we wan't the ability to display array members as [0] - <ObjectName> for example.
		// But we can't use PointerType::Array for that, because that's set to Property.
		bool IsArrayMember = false;

		bool IsValid() { return !(Ptr == 0 && Prop == 0); };
		FString GetName();
		FString GetAuthoredName() { return !NameOverwrite.IsEmpty() ? NameOverwrite : GetName(); }
		FString GetDisplayName();
		bool IsExpandable();
		FString GetPropertyType();
		FString GetCPPType();
		int GetSize();
		bool CanBeOpened() { return IsExpandable() && !IsEmpty(); };

		int GetMembers(TArray<PropertyItem>* MemberArray);
		bool IsEmpty() { return !GetMembers(0); }
		int GetMemberCount() { return GetMembers(0); }
	};

	struct PropertyItemCategory {
		FString Name;
		TArray<PropertyItem> Items;
	};

	struct MemberPath {
		FString PathString;
		PropertyItem CachedItem;

		MemberPath() {};
		bool UpdateItemFromPath(TArray<PropertyItem>& Items);
	};

	PropertyItem MakeObjectItem(void* _Ptr);
	PropertyItem MakeObjectItemNamed(void* _Ptr, FString _NameOverwrite, FString NameID = "");
	PropertyItem MakeArrayItem(void* _Ptr, FProperty* _Prop, int _Index, bool IsObjectProp = false);
	PropertyItem MakePropertyItem(void* _Ptr, FProperty* _Prop);
	PropertyItem MakeFunctionItem(void* _Ptr, UFunction* _Function);
	PropertyItem MakePropertyItemNamed(void* _Ptr, FProperty* _Prop, FString Name, FString DisplayName = "");
	#define PropertyWatcherMakeStructItem(StructType, _Ptr) { PropertyWatcher::PointerType::Struct, _Ptr, 0, "", StaticStruct<StructType>() }
	#define PropertyWatcherMakeStructItemNamed(StructType, _Ptr, _NameOverwrite) { PropertyWatcher::PointerType::Struct, _Ptr, 0, _NameOverwrite, StaticStruct<StructType>() }

	//

	void Update(FString WindowName, TArray<PropertyItemCategory>& CategoryItems, TArray<MemberPath>& WatchedMembers, UWorld* World, bool* IsOpen, bool* WantsToSave, bool* WantsToLoad);
	FString ConvertWatchedMembersToString(TArray<MemberPath>& WatchedMembers);
	void LoadWatchedMembersFromString(FString String, TArray<MemberPath>& WatchedMembers);
}

#ifdef PROPERTY_WATCHER_INTERNAL

namespace PropertyWatcher {
	struct SimpleSearchParser {
		enum Modifier {
			Mod_Exact = 1,    // +word
			Mod_Regex,        // regex:, reg:, r:
			Mod_Equal,        // =value
			Mod_Greater,      // >value
			Mod_Less,         // <value
			Mod_GreaterEqual, // >=value
			Mod_LessEqual,    // <=value
		};

		enum Operator {
			OP_And = 0,       // wordA wordB
			OP_Or,            // wordA | wordB
			OP_Not,           // !word
		};

		struct Test {
			FString Ident;
			FName Column;
			Modifier Mod;
		};

		enum CommandType {
			Command_Test,
			Command_Op,
			Command_Store,
		};

		struct Command {
			CommandType Type;
			Test Tst;
			Operator Op;

			// For debugging.
			FString ToString() {
				if (Type == Command_Test) {
					FString s = Tst.Column.ToString() + ": " + Tst.Ident;
					if (Tst.Mod) {
						if (Tst.Mod == Mod_Exact) s += "[Exact]";
						else if (Tst.Mod == Mod_Regex) s += "[Regex]";
						else if (Tst.Mod == Mod_Equal) s += "[Equal]";
						else if (Tst.Mod == Mod_Greater) s += "[Greater]";
						else if (Tst.Mod == Mod_Less) s += "[Less]";
						else if (Tst.Mod == Mod_GreaterEqual) s += "[GreaterEqual]";
						else if (Tst.Mod == Mod_LessEqual) s += "[LessEqual]";
					}
					return s;
				} else if (Type == Command_Op) {
					if (Op == OP_And) return "AND";
					else if (Op == OP_Or) return "OR";
					else if (Op == OP_Not) return "NOT";
				} else if (Type == Command_Store) return "STORE";
				return "";
			}
		};

		TArray<Command> Commands;

		void ParseExpression(FString SearchString, TArray<FString> _Columns);
		bool ApplyTests(TMap<FName, FString>& ColumnTexts);
	};

	//

	struct VisitedPropertyInfo {
		void* Address;
		FString CPPType; // @ToDo: Find a better way to compare types.

		static VisitedPropertyInfo FromItem(PropertyItem& Item) { 
			return { Item.Ptr, Item.GetCPPType() }; 
		};
		bool Compare(PropertyItem& Item) { 
			return Address == Item.Ptr && CPPType == Item.GetCPPType(); 
		}
		bool Compare(VisitedPropertyInfo& Info) { 
			return Address == Info.Address && CPPType == Info.CPPType;
		}
	};

	struct TreeState {
		// Watch item vars.

		int CurrentWatchItemIndex = -1;
		bool WatchItemGotDeleted; // Out
		bool MoveHappened; // Out
		int MoveFrom, MoveTo; // Out

		bool RenameHappened; // Out
		FString* PathStringPtr;
		char* StringBuffer;
		int StringBufferSize;

		// Global options.

		SimpleSearchParser SearchParser;
		bool SearchFilterActive;

		bool EnableClassCategoriesOnObjectItems;
		bool ListFunctionsOnObjectItems;

		// Temp options that get set by items

		bool ForceToggleNodeOpenClose;
		bool ForceToggleNodeMode; // true is open, false is close
		int ForceToggleNodeStackIndexLimit; // Force toggle only applied up to certain depth.
		TArray<VisitedPropertyInfo> VisitedPropertiesStack; // For open all nodes recursion safety.

		bool ForceInlineChildItems;
		int InlineStackIndexLimit;
		int InlineMemberPathIndexOffset;

		//

		int ItemDrawCount; // Info.

		// Visual helper.
		bool AddressWasHovered;
		bool DrawHoveredAddress;
		void* HoveredAddress;

		//

		void EnableForceToggleNode(bool Mode, int StackIndexLimit) {
			ForceToggleNodeOpenClose = true;
			ForceToggleNodeMode = Mode;
			ForceToggleNodeStackIndexLimit = StackIndexLimit;

			VisitedPropertiesStack.Empty();
		}

		void DisableForceToggleNode() {
			ForceToggleNodeOpenClose = false;
		}

		bool IsForceToggleNodeActive(int StackIndex) {
			return ForceToggleNodeOpenClose && (StackIndex <= ForceToggleNodeStackIndexLimit);
		}
	};

	void DrawItemRow(TreeState& State, PropertyItem Item, TArray<FString>& CurrentPath, int StackIndex = 0);
	void DrawItemChildren(TreeState& State, PropertyItem Item, TArray<FString>& CurrentMemberPath, int StackIndex);
	FString GetColumnCellText(TreeState& State, PropertyItem& Item, FName ColumnName, TArray<FString>* CurrentMemberPath = 0, int* StackIndex = 0);
	FString GetValueStringFromItem(PropertyItem& Item);
	void DrawPropertyValue(PropertyItem& Item);

	void* ContainerToValuePointer(PointerType Type, void* ContainerPtr, FProperty* MemberProp);

	//

	struct TreeNodeState {
		// In.

		bool HasBranches;
		VisitedPropertyInfo ItemInfo;

		bool PushTextColor;
		ImVec4 TextColor;

		int VisualStackIndex = -1;
		bool OverrideNoTreePush;

		// Options.

		bool IsOpen;
		bool ActivatedForceToggleNodeOpenClose;
		bool InlineChildren;
		bool ItemIsInlined;
	};

	struct ColumnInfo {
		FString Name;
		FString DisplayName;
		int Flags;
		float InitWidth = 0.0f;
	};

	struct ColumnInfos {
		TArray<ColumnInfo> Infos;

		ColumnInfo& GetByName(FString _Name) {
			for (int i = 0; i < Infos.Num() - 1; i++)
				if (Infos[i].Name == _Name)
					return Infos[i];
		}
		int GetIndexByName(FString _Name) {
			for (int i = 0; i < Infos.Num() - 1; i++)
				if (Infos[i].Name == _Name)
					return i;
			return -1;
		}
		TArray<FString> GetSearchNameArray() {
			TArray<FString> Names;
			for (auto It : Infos)
				if (!It.Name.IsEmpty())
					Names.Add(It.Name);
			return Names;
		}
	};

	void SetTableRowBackgroundByStackIndex(int StackIndex);

	// Makes the tree node widget for property name. Handles expansion/inlining/column management and so on.
	bool BeginTreeNode(FString NameID, FString DisplayName, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags = 0);
	void TreeNodeSetInline(TreeNodeState& NodeState, TreeState& State, int CurrentMemberPathLength, int StackIndex, bool Inline, int InlineStackDepth);
	void EndTreeNode(TreeNodeState& NodeState, TreeState& State);

	// Helper to make category/section rows.
	bool BeginSection(FString Name, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags = 0);
	void EndSection(TreeNodeState& NodeState, TreeState& State);

	TArray<FName> GetClassFunctionList(UClass* Class);
	TArray<UFunction*> GetObjectFunctionList(UObject* Obj);
	FString GetItemMetadataCategory(PropertyItem& Item);
	bool GetItemColor(PropertyItem& Item, ImVec4& Color);
	bool GetObjFromObjPointerProp(PropertyItem& Item, UObject*& Object);

	//

	struct SectionHelper {
		TArray<FString> Names;

		bool            Enabled = false;
		TArray<FString> SectionNames;
		TArray<int>     StartIndexes;
		int             CurrentSectionIndex = 0;

		void Init() {
			Enabled = true;

			for (int i = 0; i < Names.Num(); i++) {
				if (!SectionNames.Contains(Names[i])) {
					SectionNames.Push(Names[i]);
					StartIndexes.Push(i);
				}
			}
			StartIndexes.Push(Names.Num()); // For last section to get correct member end index.

			if (SectionNames.Num() < 2)
				Enabled = false;
		}

		int GetSectionCount() {
			return SectionNames.Num();
		};

		FString GetSectionInfo(int SectionIndex, int& MemberStartIndex, int& MemberEndIndex) {
			MemberStartIndex = StartIndexes[SectionIndex];
			MemberEndIndex = StartIndexes[SectionIndex + 1];
			return SectionNames[SectionIndex];
		}
	};

	//

	#define Ansi(ws) StringCast<char>(ws).Get()
	#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))
		extern const char* SearchBoxHelpText;
		extern const char* HelpText;

	#if !defined defer	
		struct defer_dummy {};
		template <class T> struct deferrer {
			T f;
			deferrer(T f) : f(f) {}
			~deferrer() { f(); }
		};
		template <class T> deferrer<T> operator*(defer_dummy, T f) { return { f }; }
	#define DEFER_(LINE) zz_defer##LINE	
	#define DEFER(LINE) DEFER_(LINE)	
	#define defer auto DEFER(__LINE__) = defer_dummy{} *[&]()
	#endif	

	#define ImGui_StoA(ws) StringCast<char>(ws).Get()

	namespace ImGuiAddon {
		bool InputText(const char* label, TArray<char>& str, ImGuiInputTextFlags flags = 0, ::ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
		bool InputTextWithHint(const char* label, const char* hint, TArray<char>& str, ImGuiInputTextFlags flags = 0, ::ImGuiInputTextCallback callback = NULL, void* user_data = NULL);

		bool InputString(FString Label, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue);
		bool InputStringWithHint(FString Label, FString Hint, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue);

		void QuickTooltip(FString TooltipText, ImGuiHoveredFlags Flags = ImGuiHoveredFlags_DelayNormal);
	}
}

#endif

#endif

/*
	MIT License

	Copyright (c) 2023 Roy Thieme

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/