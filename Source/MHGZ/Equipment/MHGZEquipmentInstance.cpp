// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZEquipmentInstance.h"
#include "MHGZEquipmentDefinition.h"

void UMHGZEquipmentInstance::SocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName)
{
	if (!Accessory || !CanSocketAccessory(Accessory, SocketName)) return;

	Accessory->SetStatus(EEquipmentStatus::Socketed);
	SocketedAccessories.Add(SocketName, Accessory);
}

void UMHGZEquipmentInstance::RemoveAccessory(FName SocketName)
{
	if (TObjectPtr<UMHGZEquipmentInstance>* Found = SocketedAccessories.Find(SocketName))
	{
		(*Found)->SetStatus(EEquipmentStatus::InStorage);
		SocketedAccessories.Remove(SocketName);
	}
}

bool UMHGZEquipmentInstance::CanSocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName) const
{
	if (!Accessory || !Definition) return false;

	// 查找孔位等级
	const FEquipmentSocket* Socket = Definition->Sockets.FindByPredicate(
		[SocketName](const FEquipmentSocket& S) { return S.SocketName == SocketName; });

	if (!Socket) return false;

	// 饰品等级 ≤ 孔位等级
	return Accessory->Definition->RarityLevel <= Socket->SocketLevel;
}
