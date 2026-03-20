// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "ToonShadowColorCommandlet.generated.h"

UCLASS()
class TOONCODEX573EDITOR_API UToonShadowColorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UToonShadowColorCommandlet();

	virtual int32 Main(const FString& Params) override;
};
