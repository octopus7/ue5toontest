// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "ToonPostProcessMaterialCommandlet.generated.h"

UCLASS()
class TOONCODEX573EDITOR_API UToonPostProcessMaterialCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UToonPostProcessMaterialCommandlet();

	virtual int32 Main(const FString& Params) override;
};
