// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TacticalPhysicalMaterial.generated.h"

/**
 * Physical material carrying wallbang data. Density is the cost multiplier per centimetre
 * of material traversed (see FPenetrationTierParams::Power).
 * Reference values: glass 0.1, drywall 0.4, wood 0.6, concrete 2.0, sheet metal 3.0.
 */
UCLASS(BlueprintType)
class TACTICALDEPLOYMENT_API UTacticalPhysicalMaterial : public UPhysicalMaterial
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration", meta = (ClampMin = "0"))
	float PenetrationDensity = 1.f;

	/** Map boundaries, bomb-site boxes, etc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration")
	bool bImpenetrable = false;

	/** Density for any material, including engine defaults that are not tactical materials. */
	static float GetDensity(const UPhysicalMaterial* Material)
	{
		constexpr float UnknownMaterialDensity = 1.5f;
		const UTacticalPhysicalMaterial* Tactical = Cast<UTacticalPhysicalMaterial>(Material);
		if (!Tactical)
		{
			return UnknownMaterialDensity;
		}
		return Tactical->bImpenetrable ? TNumericLimits<float>::Max() : Tactical->PenetrationDensity;
	}
};
