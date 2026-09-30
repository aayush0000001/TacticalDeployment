// Shim: primary data asset base.
#pragma once
#include "CoreMinimal.h"

struct FPrimaryAssetId
{
	FPrimaryAssetId() = default;
	FPrimaryAssetId(FName InType, FName InName) : PrimaryAssetType(InType), PrimaryAssetName(InName) {}
	FName PrimaryAssetType;
	FName PrimaryAssetName;
};

class UDataAsset : public UObject {};

class UPrimaryDataAsset : public UDataAsset
{
public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const { return FPrimaryAssetId(); }
};
