/*
	PropertyWatcher - v0.3.4 - http://github.com/guitarfreak/PropertyWatcher
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
			// This should be set to true once at the beginning.
			// It's not crucial to set it, but among other things it clears the internal actors array after a restart.
			// If you looked up some actors in the actors tab and didn't clear them after a restart you will get a crash.
			// This happens because internally we use static TArray<> which doesn't get cleared on a restart, only on the first compile.
			bool PropertyWatcherInit = false;

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
			PropertyWatcher::Update("Actors", Objects, WatchedMembers, GetWorld(), &PropertyWatcherIsOpen, &WantsToSave, &WantsToLoad, PropertyWatcherInit);

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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
#endif

#if !UE_SERVER

#ifndef PROPERTY_WATCHER_H_INCLUDE
#define PROPERTY_WATCHER_H_INCLUDE

#include "imgui.h"

namespace PropertyWatcher {
	
	// FAnsiStringView seems too verbose.
	// This gets us closer to the size of "FString".
	typedef FAnsiStringView FAView;

	// Writing .GetData() everywhere looks unpleasant.
	UE_NODISCARD FORCEINLINE const char* operator*(FAView a) { return a.GetData(); }

	// These functions don't exist with a search case parameter for string views.
	// So we have to add them ourselfs.
	template <typename CharType>
	inline int32 StringView_Find(TStringView<CharType>& View, TStringView<CharType> Search, ESearchCase::Type SearchCase = ESearchCase::Type::IgnoreCase, int32 StartPosition = 0) {
		int32 Index = UE::String::FindFirst(View.RightChop(StartPosition), Search, SearchCase);
		return Index == INDEX_NONE ? INDEX_NONE : Index + StartPosition;
	}

	template <typename CharType>
	inline bool StringView_Contains(TStringView<CharType>& View, TStringView<CharType> Search, ESearchCase::Type SearchCase = ESearchCase::Type::IgnoreCase) {
		return StringView_Find(View, Search, SearchCase) != INDEX_NONE;
	}

	//

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
		FAView NameOverwrite = "";
		UStruct* StructPtr = 0; // Top level structs use this as UScriptStruct and Functions as UFunction
		FAView NameIDOverwrite = ""; // Optional

		int CachedMemberCount = -1;

		bool IsValid() { return !(Ptr == 0 && Prop == 0); };
		FName GetName();
		FAView GetAuthoredName();
		//FString GetDisplayName(); // Unused.

		bool IsExpandable();
		FAView GetPropertyType();
		FAView GetCPPType();
		int GetSize();
		bool CanBeOpened() { return IsExpandable() && !IsEmpty(); };

		int GetMembers(TArray<PropertyItem>* MemberArray);
		bool IsEmpty() { return !GetMemberCount(); }
		int GetMemberCount();
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
	PropertyItem MakeObjectItemNamed(void* _Ptr, const char* _NameOverwrite, FAView NameID = "");
	PropertyItem MakeObjectItemNamed(void* _Ptr, FString _NameOverwrite, FAView NameID = "");
	PropertyItem MakeObjectItemNamed(void* _Ptr, FAView _NameOverwrite, FAView NameID = "");
	PropertyItem MakeArrayItem(void* _Ptr, FProperty* _Prop, int _Index, bool IsObjectProp = false);
	PropertyItem MakePropertyItem(void* _Ptr, FProperty* _Prop);
	PropertyItem MakeFunctionItem(void* _Ptr, UFunction* _Function);
	PropertyItem MakePropertyItemNamed(void* _Ptr, FProperty* _Prop, FAView Name, FAView NameID = "");
	#define PropertyWatcherMakeStructItem(StructType, _Ptr) { PropertyWatcher::PointerType::Struct, _Ptr, 0, "", StaticStruct<StructType>() }
	#define PropertyWatcherMakeStructItemNamed(StructType, _Ptr, _NameOverwrite) { PropertyWatcher::PointerType::Struct, _Ptr, 0, _NameOverwrite, StaticStruct<StructType>() }

	//

	void Update(FString WindowName, TArray<PropertyItemCategory>& CategoryItems, TArray<MemberPath>& WatchedMembers, UWorld* World, bool* IsOpen, bool* WantsToSave, bool* WantsToLoad, bool Init = false);
	FString ConvertWatchedMembersToString(TArray<MemberPath>& WatchedMembers);
	void LoadWatchedMembersFromString(FString String, TArray<MemberPath>& WatchedMembers);
}

#endif // PROPERTY_WATCHER_H_INCLUDE

#ifdef PROPERTY_WATCHER_INTERNAL
#undef PROPERTY_WATCHER_INTERNAL

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
			FAView Ident;
			Modifier Mod;
			int ColumnID = 0; // ColumnID_Name
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

			FAView ToString(); // For debugging.
		};

		TArray<Command> Commands;

		void ParseExpression(FAView SearchString, TArray<FAView> _Columns);
		bool ApplyTests(struct CachedColumnText& ColumnTexts);
	};

	//

	struct VisitedPropertyInfo {
		void* Address;

		void Set(PropertyItem& Item) { Address = Item.Ptr; };
		bool Compare(PropertyItem& Item) { return Address == Item.Ptr; }
		bool Compare(VisitedPropertyInfo& Info) { return Address == Info.Address; }
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

		FFloatInterval ScrollRegionRange; // For item culling.

		// Global options.

		SimpleSearchParser SearchParser;
		bool SearchFilterActive;

		bool EnableClassCategoriesOnObjectItems;
		bool ListFunctionsOnObjectItems;
		bool ShowObjectNamesOnAllProperties;

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

		void EnableForceToggleNode(bool Mode, int StackIndexLimit);
		void DisableForceToggleNode() { ForceToggleNodeOpenClose = false; }
		bool IsForceToggleNodeActive(int StackIndex) { return ForceToggleNodeOpenClose && (StackIndex <= ForceToggleNodeStackIndexLimit); }
		bool ItemIsInfiniteLooping(VisitedPropertyInfo& PropertyInfo);
		bool IsCurrentItemVisible() { return ScrollRegionRange.Contains(ImGui::GetCursorPosY()); }
	};

	void DrawItemRow(TreeState& State, PropertyItem& Item, TInlineComponentArray<FAView>& CurrentPath, int StackIndex = 0);
	void DrawItemChildren(TreeState& State, PropertyItem& Item, TInlineComponentArray<FAView>& CurrentMemberPath, int StackIndex);
	FAView GetColumnCellText(PropertyItem& Item, int ColumnID, TreeState* State = 0, TInlineComponentArray<FAView>* CurrentMemberPath = 0, int* StackIndex = 0);
	bool ItemHasMetaData(PropertyItem& Item);
	FAView GetValueStringFromItem(PropertyItem& Item);
	void DrawPropertyValue(PropertyItem& Item);

	void* ContainerToValuePointer(PointerType Type, void* ContainerPtr, FProperty* MemberProp);

	void DrawItemChildren(TreeState& State, PropertyItem&& Item, TInlineComponentArray<FAView>& CurrentMemberPath, int StackIndex) {
		PropertyItem& Temp = Item;
		return DrawItemChildren(State, Temp, CurrentMemberPath, StackIndex);
	}
	void DrawItemRow(TreeState& State, PropertyItem&& Item, TInlineComponentArray<FAView>& CurrentPath, int StackIndex = 0) {
		PropertyItem& Temp = Item;
		return DrawItemRow(State, Temp, CurrentPath, StackIndex);
	}

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

	enum ColumnID {
		ColumnID_Name = 0,
		ColumnID_Value,
		ColumnID_Metadata,
		ColumnID_Type,
		ColumnID_Cpptype,
		ColumnID_Class,
		ColumnID_Category,
		ColumnID_Address,
		ColumnID_Size,
		ColumnID_Remove,

		ColumnID_MAX_SIZE,
	};

	struct ColumnInfo {
		int ID;
		FAView Name;
		FAView DisplayName;
		int Flags;
		float InitWidth = 0.0f;
	};

	struct CachedColumnText {
		bool ColumnTextsCached[ColumnID_MAX_SIZE] = {};
		FAView ColumnTexts[ColumnID_MAX_SIZE];

		void Add(int ColumnID, FAView Text) {
			ColumnTextsCached[ColumnID] = true;
			ColumnTexts[ColumnID] = Text;
		}

		FAView* Get(int ColumnID) {
			if (ColumnTextsCached[ColumnID])
				return &ColumnTexts[ColumnID];
			return 0;
		}
	};

	struct ColumnInfos {
		TArray<ColumnInfo> Infos;

		TArray<FAView> GetSearchNameArray() {
			TArray<FAView> Result;
			for (auto It : Infos)
				if (!It.Name.IsEmpty())
					Result.Add(It.Name);
			return Result;
		}
	};

	void SetTableRowBackgroundByStackIndex(int StackIndex);

	// Makes the tree node widget for property name. Handles expansion/inlining/column management and so on.
	bool BeginTreeNode(const char* NameID, const char* DisplayName, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags = 0);
	void TreeNodeSetInline(TreeNodeState& NodeState, TreeState& State, int CurrentMemberPathLength, int StackIndex, int InlineStackDepth);
	void EndTreeNode(TreeNodeState& NodeState, TreeState& State);

	// Helper to make category/section rows.
	bool BeginSection(FAView Name, TreeNodeState& NodeState, TreeState& State, int StackIndex, int ExtraFlags = 0);
	void EndSection(TreeNodeState& NodeState, TreeState& State);

	TArray<FName> GetClassFunctionList(UClass* Class);
	TArray<UFunction*> GetObjectFunctionList(UObject* Obj);
	FAView GetItemMetadataCategory(PropertyItem& Item);
	bool GetItemColor(PropertyItem& Item, ImVec4& Color);
	bool GetObjFromObjPointerProp(PropertyItem& Item, UObject*& Object);

	//

	extern const char* SearchBoxHelpText;
	extern const char* HelpText;

	#define ImGui_StoA(ws) StringCast<char>(ws).Get()
	#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))
	#ifndef defer
	#define defer ON_SCOPE_EXIT
	#endif

	namespace ImGuiAddon {
		bool InputText(const char* label, TArray<char>& str, ImGuiInputTextFlags flags = 0, ::ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
		bool InputTextWithHint(const char* label, const char* hint, TArray<char>& str, ImGuiInputTextFlags flags = 0, ::ImGuiInputTextCallback callback = NULL, void* user_data = NULL);

		bool InputString(FString Label, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue);
		bool InputStringWithHint(FString Label, FString Hint, FString& String, TArray<char>& StringBuffer, ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue);

		void QuickTooltip(FString TooltipText, ImGuiHoveredFlags Flags = ImGuiHoveredFlags_DelayNormal);
	}

	//

	// Stable pointers, doubles in size, allocates/frees by moving index.
	struct TempMemoryPool {
		struct MemBucket {
			char* Data = 0;
			int Size = 0;
			int Position = 0;

			FORCEINLINE bool MemoryFits(int Count) { return Position + Count < Size; };
			char* Get(int Count);
			FORCEINLINE void Free(int Count) { Position -= Count; }
		};

		struct Marker {
			int BucketIndex;
			int DataPosition;
		};

		TInlineComponentArray<MemBucket, 16> Buckets;
		TInlineComponentArray<Marker, 32> Markers;

		bool IsInitialized = false;
		int StartSize = 1024;
		int CurrentBucketIndex = 0;

		void Init(int _StartSize);
		void GoToNextBucket();
		void GoToPrevBucket() { CurrentBucketIndex--; }
		FORCEINLINE MemBucket& GetCurrentBucket() { return Buckets[CurrentBucketIndex]; }
		void AddBucket();
		void ClearAll();
		char* Get(int Count);
		void Free(int Count);

		void PushMarker();
		void FreeToMarker();
		void PopMarker(bool Free = true);

		FAView Printf(const char* Fmt, ...);
		FAView Append(FAView a, FAView b) { return Printf("%s%s", a.GetData(), b.GetData()); }
		void Append(FAView* a, FAView b) { *a = Printf("%s%s", a->GetData(), b.GetData()); }
		FAView CToA(const TCHAR* SrcBuffer, int SrcLen);
		FAView SToA(FString&& String) { return CToA(*String, String.Len()); }
		FAView SToA(FString& String) { return CToA(*String, String.Len()); }
		FAView NToA(FName Name);

		// @Note: 512 bytes should be fine? If not the text gets cut off, shouldn't be a big deal.
		#define TMemBuilderS(Name, Size) TStringBuilderBase<ANSICHAR> Name(TMem.Get(Size), Size)
		#define TMemBuilder(Name) TMemBuilderS(Name, 512)
	};

	TempMemoryPool TMem;
	int TMemoryStartSize = 1024;

	//

	struct SectionHelper {
		FName                         CurrentName = NAME_None;
		int                           CurrentIndex = 0;
		bool                          Enabled = false;
		TInlineComponentArray<FAView> SectionNames;
		TInlineComponentArray<int>    StartIndexes;
		int                           CurrentSectionIndex = 0;

		FORCEINLINE void Add(FName Name) {
			if (Name != CurrentName) {
				CurrentName = Name;
				SectionNames.Add(TMem.NToA(CurrentName));
				StartIndexes.Push(CurrentIndex);
			}
			CurrentIndex++;
		}

		void Init() {
			StartIndexes.Push(CurrentIndex); // For last section to get correct member end index.
			if (SectionNames.Num() >= 2)
				Enabled = true;
		}

		int GetSectionCount() { return SectionNames.Num(); };

		FAView GetSectionInfo(int SectionIndex, int& MemberStartIndex, int& MemberEndIndex) {
			MemberStartIndex = StartIndexes[SectionIndex];
			MemberEndIndex = StartIndexes[SectionIndex + 1];
			return SectionNames[SectionIndex];
		}
	};
}


#endif // PROPERTY_WATCHER_INTERNAL
#endif // UE_SERVER

#if defined(__clang__)
#pragma clang diagnostic pop
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