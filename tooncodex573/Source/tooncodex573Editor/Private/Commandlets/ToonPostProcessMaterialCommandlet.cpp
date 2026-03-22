// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ToonPostProcessMaterialCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/EngineTypes.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "tooncodex573Editor.h"

namespace
{
	struct FToonPostProcessMaterialSettings
	{
		FString AssetPath = TEXT("/Game/ToonTests/PostProcess/M_PP_ToonTonemapCompensate");
		uint8 StencilRefValue = 1;
		float EffectStrength = 1.0f;
		float SaturationAmount = 1.15f;
		float Gain = 1.0f;
		float Gamma = 1.0f;
		bool bEnableStencilTest = true;
		bool bRecompile = true;
		bool bSave = true;
	};

	bool ParseSettings(const FString& Params, FToonPostProcessMaterialSettings& OutSettings)
	{
		FString AssetPathValue;
		if (FParse::Value(*Params, TEXT("AssetPath="), AssetPathValue))
		{
			OutSettings.AssetPath = AssetPathValue;
		}

		int32 StencilRefValue = static_cast<int32>(OutSettings.StencilRefValue);
		if (FParse::Value(*Params, TEXT("StencilRef="), StencilRefValue))
		{
			if ((StencilRefValue < 0) || (StencilRefValue > 255))
			{
				UE_LOG(LogTooncodex573Editor, Error, TEXT("StencilRef must be between 0 and 255."));
				return false;
			}

			OutSettings.StencilRefValue = static_cast<uint8>(StencilRefValue);
		}

		FParse::Value(*Params, TEXT("EffectStrength="), OutSettings.EffectStrength);
		FParse::Value(*Params, TEXT("SaturationAmount="), OutSettings.SaturationAmount);
		FParse::Value(*Params, TEXT("Gain="), OutSettings.Gain);
		FParse::Value(*Params, TEXT("Gamma="), OutSettings.Gamma);

		OutSettings.bEnableStencilTest = !FParse::Param(*Params, TEXT("NoStencilTest"));
		OutSettings.bRecompile = !FParse::Param(*Params, TEXT("NoRecompile"));
		OutSettings.bSave = !FParse::Param(*Params, TEXT("NoSave"));
		return true;
	}

	bool NormalizeAssetPath(const FString& InAssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutObjectPath)
	{
		const FString AssetPath = InAssetPath.TrimStartAndEnd();
		if (!AssetPath.StartsWith(TEXT("/")))
		{
			return false;
		}

		FString ExplicitAssetName;
		if (AssetPath.Contains(TEXT(".")))
		{
			const FSoftObjectPath SoftObjectPath(AssetPath);
			OutPackageName = SoftObjectPath.GetLongPackageName();
			ExplicitAssetName = SoftObjectPath.GetAssetName();
		}
		else
		{
			OutPackageName = AssetPath;
		}

		int32 LastSlashIndex = INDEX_NONE;
		if (!OutPackageName.FindLastChar(TEXT('/'), LastSlashIndex) || LastSlashIndex <= 0 || LastSlashIndex >= OutPackageName.Len() - 1)
		{
			return false;
		}

		OutAssetName = ExplicitAssetName.IsEmpty() ? OutPackageName.Mid(LastSlashIndex + 1) : ExplicitAssetName;
		if (OutPackageName.IsEmpty() || OutAssetName.IsEmpty())
		{
			return false;
		}

		OutObjectPath = OutPackageName + TEXT(".") + OutAssetName;
		return true;
	}

	UMaterial* LoadOrCreateMaterial(const FString& PackageName, const FString& AssetName, const FString& ObjectPath)
	{
		FString ExistingPackageFilename;
		if (FPackageName::DoesPackageExist(PackageName, &ExistingPackageFilename))
		{
			if (UPackage* ExistingPackage = LoadPackage(nullptr, *PackageName, LOAD_None))
			{
				if (UMaterial* ExistingMaterial = FindObject<UMaterial>(ExistingPackage, *AssetName))
				{
					return ExistingMaterial;
				}
			}
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to create package %s"), *PackageName);
			return nullptr;
		}

		Package->FullyLoad();

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UObject* CreatedObject = Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
		UMaterial* NewMaterial = Cast<UMaterial>(CreatedObject);
		if (!NewMaterial)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to create material asset %s"), *ObjectPath);
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(NewMaterial);
		Package->MarkPackageDirty();
		return NewMaterial;
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
		if (!Package)
		{
			return false;
		}

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;

		return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	}

	template <typename TExpression>
	TExpression* AddExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
	}

	UMaterialExpressionSceneTexture* AddSceneTextureExpression(UMaterial* Material, const ESceneTextureId SceneTextureId, const int32 X, const int32 Y)
	{
		UMaterialExpressionSceneTexture* Expression = AddExpression<UMaterialExpressionSceneTexture>(Material, X, Y);
		if (Expression)
		{
			Expression->SceneTextureId = SceneTextureId;
			Expression->bFiltered = false;
		}
		return Expression;
	}

	UMaterialExpressionComponentMask* AddRgbMaskExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		UMaterialExpressionComponentMask* Expression = AddExpression<UMaterialExpressionComponentMask>(Material, X, Y);
		if (Expression)
		{
			Expression->R = true;
			Expression->G = true;
			Expression->B = true;
			Expression->A = false;
		}
		return Expression;
	}

	UMaterialExpressionConstant3Vector* AddConstant3VectorExpression(UMaterial* Material, const FLinearColor& Value, const int32 X, const int32 Y)
	{
		UMaterialExpressionConstant3Vector* Expression = AddExpression<UMaterialExpressionConstant3Vector>(Material, X, Y);
		if (Expression)
		{
			Expression->Constant = Value;
		}
		return Expression;
	}

	UMaterialExpressionScalarParameter* AddScalarParameterExpression(UMaterial* Material, const TCHAR* ParameterName, const float DefaultValue, const float SliderMin, const float SliderMax, const int32 X, const int32 Y, const int32 SortPriority)
	{
		UMaterialExpressionScalarParameter* Expression = AddExpression<UMaterialExpressionScalarParameter>(Material, X, Y);
		if (Expression)
		{
			Expression->SetParameterName(ParameterName);
			Expression->Group = TEXT("ToonTonemap");
			Expression->SortPriority = SortPriority;
			Expression->DefaultValue = DefaultValue;
			Expression->SliderMin = SliderMin;
			Expression->SliderMax = SliderMax;
		}
		return Expression;
	}

	bool ConnectDefaultOutput(UMaterialExpression* FromExpression, FExpressionInput& Input)
	{
		if (!FromExpression)
		{
			return false;
		}

		Input.Connect(0, FromExpression);
		return true;
	}

	bool ConnectMaterialProperty(UMaterial* Material, UMaterialExpression* FromExpression, const EMaterialProperty Property)
	{
		if (!Material || !FromExpression)
		{
			return false;
		}

		if (FExpressionInput* PropertyInput = Material->GetExpressionInputForProperty(Property))
		{
			PropertyInput->Connect(0, FromExpression);
			return true;
		}

		return false;
	}

	bool ConfigureMaterial(UMaterial* Material, const FToonPostProcessMaterialSettings& Settings)
	{
		Material->Modify();
		Material->MaterialDomain = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorBeforeBloom;
		Material->BlendablePriority = 0;
		Material->bIsBlendable = true;
		Material->BlendableOutputAlpha = false;
		Material->bDisablePreExposureScale = false;
		Material->bEnableStencilTest = Settings.bEnableStencilTest;
		Material->StencilCompare = Settings.bEnableStencilTest ? MSC_Equal : MSC_Always;
		Material->StencilRefValue = Settings.StencilRefValue;
		Material->bUseMaterialAttributes = false;

		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

		UMaterialExpressionSceneTexture* SceneColor = AddSceneTextureExpression(Material, PPI_PostProcessInput0, -1200, -100);
		UMaterialExpressionComponentMask* SceneColorRgb = AddRgbMaskExpression(Material, -980, -100);
		UMaterialExpressionConstant3Vector* LumaWeights = AddConstant3VectorExpression(Material, FLinearColor(0.2126f, 0.7152f, 0.0722f, 1.0f), -980, 120);
		UMaterialExpressionDotProduct* Luma = AddExpression<UMaterialExpressionDotProduct>(Material, -760, -10);
		UMaterialExpressionAppendVector* LumaRG = AddExpression<UMaterialExpressionAppendVector>(Material, -540, -40);
		UMaterialExpressionAppendVector* LumaRgb = AddExpression<UMaterialExpressionAppendVector>(Material, -320, -40);
		UMaterialExpressionScalarParameter* SaturationAmount = AddScalarParameterExpression(Material, TEXT("SaturationAmount"), Settings.SaturationAmount, 0.0f, 2.0f, -540, 180, 0);
		UMaterialExpressionScalarParameter* Gain = AddScalarParameterExpression(Material, TEXT("Gain"), Settings.Gain, 0.0f, 2.0f, -540, 320, 1);
		UMaterialExpressionScalarParameter* Gamma = AddScalarParameterExpression(Material, TEXT("Gamma"), Settings.Gamma, 0.25f, 2.0f, -540, 460, 2);
		UMaterialExpressionScalarParameter* EffectStrength = AddScalarParameterExpression(Material, TEXT("EffectStrength"), Settings.EffectStrength, 0.0f, 1.0f, -320, 600, 3);
		UMaterialExpressionLinearInterpolate* SaturationLerp = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -80, 60);
		UMaterialExpressionMultiply* GainMultiply = AddExpression<UMaterialExpressionMultiply>(Material, 160, 120);
		UMaterialExpressionPower* GammaPower = AddExpression<UMaterialExpressionPower>(Material, 400, 120);
		UMaterialExpressionLinearInterpolate* FinalBlend = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 640, 40);

		if (!SceneColor || !SceneColorRgb || !LumaWeights || !Luma || !LumaRG || !LumaRgb || !SaturationAmount || !Gain || !Gamma || !EffectStrength || !SaturationLerp || !GainMultiply || !GammaPower || !FinalBlend)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to allocate one or more post-process material expressions."));
			return false;
		}

		const bool bConnected =
			ConnectDefaultOutput(SceneColor, SceneColorRgb->Input) &&
			ConnectDefaultOutput(SceneColorRgb, Luma->A) &&
			ConnectDefaultOutput(LumaWeights, Luma->B) &&
			ConnectDefaultOutput(Luma, LumaRG->A) &&
			ConnectDefaultOutput(Luma, LumaRG->B) &&
			ConnectDefaultOutput(LumaRG, LumaRgb->A) &&
			ConnectDefaultOutput(Luma, LumaRgb->B) &&
			ConnectDefaultOutput(LumaRgb, SaturationLerp->A) &&
			ConnectDefaultOutput(SceneColorRgb, SaturationLerp->B) &&
			ConnectDefaultOutput(SaturationAmount, SaturationLerp->Alpha) &&
			ConnectDefaultOutput(SaturationLerp, GainMultiply->A) &&
			ConnectDefaultOutput(Gain, GainMultiply->B) &&
			ConnectDefaultOutput(GainMultiply, GammaPower->Base) &&
			ConnectDefaultOutput(Gamma, GammaPower->Exponent) &&
			ConnectDefaultOutput(SceneColorRgb, FinalBlend->A) &&
			ConnectDefaultOutput(GammaPower, FinalBlend->B) &&
			ConnectDefaultOutput(EffectStrength, FinalBlend->Alpha) &&
			ConnectMaterialProperty(Material, FinalBlend, MP_EmissiveColor);

		if (!bConnected)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to connect one or more post-process material expressions."));
			return false;
		}

		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		Material->MarkPackageDirty();
		return true;
	}
}

UToonPostProcessMaterialCommandlet::UToonPostProcessMaterialCommandlet()
{
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	UseCommandletResultAsExitCode = true;

	HelpDescription = TEXT("Creates or updates a stencil-masked pre-tonemap post-process material for Toon tonemap compensation tests.");
	HelpUsage = TEXT("UnrealEditor-Cmd.exe <Project.uproject> -run=ToonPostProcessMaterial -AssetPath=/Game/ToonTests/PostProcess/M_PP_ToonTonemapCompensate -StencilRef=1");
	HelpParamNames = { TEXT("AssetPath"), TEXT("StencilRef"), TEXT("EffectStrength"), TEXT("SaturationAmount"), TEXT("Gain"), TEXT("Gamma"), TEXT("NoStencilTest"), TEXT("NoRecompile"), TEXT("NoSave") };
	HelpParamDescriptions = {
		TEXT("Long package path or object path for the post-process material asset."),
		TEXT("Custom stencil reference value used when stencil testing is enabled."),
		TEXT("Blend strength between untouched scene color and compensated color."),
		TEXT("Oversaturation factor applied before tonemapping."),
		TEXT("Linear gain multiplier applied before tonemapping."),
		TEXT("Power exponent applied after gain. 1.0 leaves values unchanged."),
		TEXT("Disables material-side stencil filtering and makes the pass affect the full screen."),
		TEXT("Skips material recompilation after graph edits."),
		TEXT("Skips package save after asset edits.")
	};
}

int32 UToonPostProcessMaterialCommandlet::Main(const FString& Params)
{
	FToonPostProcessMaterialSettings Settings;
	if (!ParseSettings(Params, Settings))
	{
		return 1;
	}

	FString PackageName;
	FString AssetName;
	FString ObjectPath;
	if (!NormalizeAssetPath(Settings.AssetPath, PackageName, AssetName, ObjectPath))
	{
		UE_LOG(LogTooncodex573Editor, Error, TEXT("Invalid AssetPath: %s"), *Settings.AssetPath);
		return 1;
	}

	UE_LOG(LogTooncodex573Editor, Display, TEXT("Preparing Toon post-process material %s"), *ObjectPath);

	UMaterial* Material = LoadOrCreateMaterial(PackageName, AssetName, ObjectPath);
	if (!Material)
	{
		return 1;
	}

	if (!ConfigureMaterial(Material, Settings))
	{
		return 1;
	}

	if (Settings.bRecompile)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
	}

	if (Settings.bSave && !SaveAsset(Material))
	{
		UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to save %s"), *ObjectPath);
		return 1;
	}

	UE_LOG(
		LogTooncodex573Editor,
		Display,
		TEXT("Ready: %s | Stencil=%s:%d EffectStrength=%.3f SaturationAmount=%.3f Gain=%.3f Gamma=%.3f"),
		*ObjectPath,
		Settings.bEnableStencilTest ? TEXT("Equal") : TEXT("Disabled"),
		Settings.StencilRefValue,
		Settings.EffectStrength,
		Settings.SaturationAmount,
		Settings.Gain,
		Settings.Gamma);

	return 0;
}
