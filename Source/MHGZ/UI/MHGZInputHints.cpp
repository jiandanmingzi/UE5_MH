// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZInputHints.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"

namespace
{
FGameplayTag Tag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(Name); }

FString ControlName(const FGameplayTag& Physical)
{
	const FString Name = Physical.ToString();
	if (Name == TEXT("Input.Dodge")) return TEXT("A");
	if (Name == TEXT("Input.Modifier.Sheathed")) return TEXT("RB");
	for (const TCHAR* Control : {TEXT("Y"), TEXT("B"), TEXT("RT"), TEXT("LT")})
		if (Name.EndsWith(FString(TEXT(".")) + Control)) return Control;
	return {};
}

FString AbilityName(const UClass* Class)
{
	if (!Class) return {};
	const auto* Ability = Class->GetDefaultObject<UMHGZGameplayAbility>();
	if (Ability && !Ability->InputHintDisplayName.IsEmpty()) return Ability->InputHintDisplayName.ToString();
	static const TMap<FString, FString> Names = {
		{TEXT("BaDao"), TEXT("拔刀")}, {TEXT("BaDaoZhan"), TEXT("飞身跃入拔刀")},
		{TEXT("DrawAndSendKinsect"), TEXT("放虫拔刀")}, {TEXT("TuCi"), TEXT("突刺")},
		{TEXT("ShangLao"), TEXT("上捞斩")}, {TEXT("JiaSha"), TEXT("袈裟斩")},
		{TEXT("ErLian"), TEXT("二连斩")}, {TEXT("HengSao"), TEXT("横扫")},
		{TEXT("FeiYuan"), TEXT("飞圆斩")}, {TEXT("FeiShenYueRu"), TEXT("飞身跃入斩")},
		{TEXT("HouChe"), TEXT("后撤斩")}, {TEXT("ChongYin"), TEXT("虫印斩")},
		{TEXT("TuJinHuiXuanZhan"), TEXT("突进回旋斩")}, {TEXT("SiLianYinZhan"), TEXT("四连印斩")},
		{TEXT("JueChongJi"), TEXT("觉虫击")}, {TEXT("CaoChongZhan"), TEXT("操虫斩")},
		{TEXT("JiXiTuCi"), TEXT("急袭突刺")}, {TEXT("QiangHuaCaoChongChuanCi"), TEXT("强化操虫穿刺")},
		{TEXT("SendKinsect"), TEXT("操虫")}, {TEXT("RecallKinsect"), TEXT("收虫")},
		{TEXT("PowderVortex"), TEXT("粉尘聚爆")}, {TEXT("QianChengGanTiao"), TEXT("撑杆跳")},
		{TEXT("HouChengGanTiao"), TEXT("后撑杆跳")}, {TEXT("ZuoChengGanTiao"), TEXT("左撑杆跳")},
		{TEXT("YouChengGanTiao"), TEXT("右撑杆跳")}};
	FString Name = Class->GetName(); Name.RemoveFromStart(TEXT("GA_IG_")); Name.RemoveFromEnd(TEXT("_C"));
	if (const auto* Found = Names.Find(Name)) return *Found;
	return Class->GetDisplayNameText().ToString();
}

int32 ControlOrder(const FString& Control)
{
	if (Control == TEXT("LT")) return 0;
	if (Control == TEXT("RT")) return 1;
	if (Control == TEXT("A")) return 2;
	if (Control == TEXT("Y")) return 3;
	if (Control == TEXT("B")) return 4;
	return 5;
}
struct FRecipe
{
	FGameplayTag Output;
	TArray<FString> Controls;
	FGameplayTagContainer Context;
	bool bShortRelease = false;
};
}

FString MHGZInputHints::KeyName(const FString& Control, bool bGamepad)
{
	if (bGamepad) return Control;
	static const TMap<FString, FString> Keys = {{TEXT("A"), TEXT("空格")}, {TEXT("Y"), TEXT("鼠标左键")},
		{TEXT("B"), TEXT("鼠标右键")}, {TEXT("RT"), TEXT("Ctrl")}, {TEXT("LT"), TEXT("R")}, {TEXT("RB"), TEXT("Shift")}};
	const auto* Found = Keys.Find(Control);
	return Found ? *Found : Control;
}

TArray<FMHGZInputHint> MHGZInputHints::Build(UMHGZAbilitySystemComponent& ASC,
	const UWeaponInputProfile& Profile, bool bGamepad)
{
	TArray<FMHGZInputHint> Rows;
	if (UMHGZGameplayAbility::HasPlayerActionInputLock(&ASC)) return Rows;
	FGameplayTagContainer Owned;
	ASC.GetOwnedGameplayTags(Owned);
	if (Owned.HasTag(Tag(TEXT("Combat.State.Dead"))) || Owned.HasTag(Tag(TEXT("Combat.State.Hitstun")))
		|| Owned.HasTag(Tag(TEXT("Combat.State.Knockdown")))) return Rows;
	TArray<FRecipe> Recipes;
	const auto AddRecipe = [&](FGameplayTag Output, TArray<FString> Controls,
		const FGameplayTagContainer& Required, const FGameplayTagContainer& Blocked, bool bShortRelease)
	{
		if (Controls.IsEmpty() || Controls.Contains(FString())) return;
		Controls.Sort([](const FString& A, const FString& B) { return ControlOrder(A) < ControlOrder(B); });
		FGameplayTagContainer Context = Owned;
		for (const auto* Aim : {TEXT("Combat.State.Aiming.Kinsect"), TEXT("Combat.State.Aiming.Action"), TEXT("Combat.State.Aiming.Slinger")})
			Context.RemoveTag(Tag(Aim));
		if (Controls.Contains(TEXT("LT"))) Context.AddTag(Tag(Owned.HasTag(Tag(TEXT("Combat.State.Sheathed")))
			? TEXT("Combat.State.Aiming.Slinger") : TEXT("Combat.State.Aiming.Kinsect")));
		if (Controls.Contains(TEXT("RT"))) Context.AddTag(Tag(TEXT("Combat.State.Aiming.Action")));
		if (!Context.HasAll(Required) || Context.HasAny(Blocked)) return;
		if (Recipes.ContainsByPredicate([&](const auto& R) { return R.Output == Output; })) return;
		Recipes.Add({Output, MoveTemp(Controls), MoveTemp(Context), bShortRelease});
	};
	for (const FWeaponChordDefinition& Chord : Profile.Chords)
	{
		TArray<FString> Controls;
		for (const auto& T : Chord.TriggerControls) Controls.Add(ControlName(T));
		for (const auto& T : Chord.RequiredHeldModifiers) Controls.Add(ControlName(T));
		AddRecipe(Chord.OutputTag, MoveTemp(Controls), Chord.RequiredContextTags, Chord.BlockedContextTags,
			Chord.DispatchPolicy == EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed);
	}
	// 未被单键Chord接管的Y/B等原始标签仍由原输入路由直接派发。
	for (const auto& Pair : Profile.RawActionToPhysicalInputTag)
		if (Pair.Value.MatchesTag(Tag(TEXT("Input.Weapon")))) AddRecipe(Pair.Value, {ControlName(Pair.Value)}, {}, {}, false);
	const auto* Coordinator = ASC.GetActiveComboCoordinator();
	for (const FRecipe& Recipe : Recipes)
	{
		FString Keys;
		for (const auto& Control : Recipe.Controls) { if (!Keys.IsEmpty()) Keys += TEXT("+"); Keys += KeyName(Control, bGamepad); }
		if (Recipe.bShortRelease) Keys += TEXT("（短按松开）");
		const int32 Group = Recipe.Controls.Contains(TEXT("LT")) ? 4 : Recipe.Controls.Contains(TEXT("RT")) ? 3
			: Recipe.Controls.Contains(TEXT("RB")) ? 5 : Recipe.Controls.Contains(TEXT("B")) ? 2 : Recipe.Controls.Contains(TEXT("Y")) ? 1 : 0;
		const int32 ButtonOrder = Recipe.Controls.Contains(TEXT("A")) ? 1 : Recipe.Controls.Contains(TEXT("Y")) ? 2 : Recipe.Controls.Contains(TEXT("B")) ? 3 : 0;
		const int32 BaseOrder = Group * 100 + (Recipe.Controls.Num() - 1) * 20 + ButtonOrder * 5;
		if (!Recipe.Output.MatchesTag(Tag(TEXT("Input.Weapon"))))
		{
			const auto Handle = ASC.FindAbilityHandleByInputTag(Recipe.Output);
			const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
			if (!Spec || !Spec->Ability || !Spec->Ability->CanActivateAbility(Handle, ASC.AbilityActorInfo.Get())) continue;
			FString Name;
			if (Recipe.Output == Tag(TEXT("Input.Dodge"))) Name = TEXT("翻滚");
			else if (Recipe.Output == Tag(TEXT("Input.AirDodge"))) Name = TEXT("空回");
			else if (Recipe.Output == Tag(TEXT("Input.Sheathe"))) Name = TEXT("收刀");
			if (!Name.IsEmpty()) Rows.Add({Keys, Name, BaseOrder});
			continue;
		}
		if (!Coordinator) continue;
		TSet<TSubclassOf<UGameplayAbility>> Seen;
		for (const auto Direction : {EDirectionalInput::None, EDirectionalInput::Forward, EDirectionalInput::Back, EDirectionalInput::Left, EDirectionalInput::Right})
		{
			FWeaponInputSnapshot Input;
			Input.ResolvedInputTag = Recipe.Output; Input.ContextTags = Recipe.Context; Input.Direction = Direction;
			const auto* Transition = Coordinator->PreviewInputHint(Input);
			if (!Transition || !Transition->AbilityClass || Seen.Contains(Transition->AbilityClass)) continue;
			Seen.Add(Transition->AbilityClass);
			const TCHAR* Prefix = Direction == EDirectionalInput::Forward ? TEXT("前+") : Direction == EDirectionalInput::Back ? TEXT("后+")
				: Direction == EDirectionalInput::Left ? TEXT("左+") : Direction == EDirectionalInput::Right ? TEXT("右+") : TEXT("");
			Rows.Add({FString(Prefix) + Keys, AbilityName(Transition->AbilityClass), BaseOrder + int32(Direction)});
		}
	}
	if (Owned.HasTag(Tag(TEXT("Combat.State.Sheathed"))) && Owned.HasTag(Tag(TEXT("Combat.State.Grounded"))))
	{
		const auto* Attributes = ASC.GetSet<UMHGZAttributeSet>();
		if (Attributes && Attributes->GetStamina() > 0.f) Rows.Add({TEXT("前+") + KeyName(TEXT("RB"), bGamepad), TEXT("疾跑"), 501});
	}
	if (Owned.HasTag(Tag(TEXT("Combat.State.Unsheathed")))) Rows.Add({KeyName(TEXT("LT"), bGamepad), TEXT("猎虫瞄准"), 400});
	Rows.Sort([](const auto& A, const auto& B) { return A.SortOrder == B.SortOrder ? A.Keys < B.Keys : A.SortOrder < B.SortOrder; });
	return Rows;
}
