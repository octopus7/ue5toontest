// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ToonShadowColorCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "SceneTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "tooncodex573Editor.h"

namespace
{
	struct FToonShadowColorCommandletSettings
	{
		FString AssetPath = TEXT("/Game/ToonTests/M_ToonShadowColorSmoke");
		FLinearColor BaseColor = FLinearColor(1.0f, 0.9f, 0.75f, 1.0f);
		FLinearColor ShadowColor = FLinearColor(0.05f, 0.1f, 0.8f, 1.0f);
		float Metallic = 0.0f;
		float Specular = 0.0f;
		float Roughness = 0.85f;
		bool bRecompile = true;
		bool bSave = true;
	};

	bool ParseColorString(const FString& InValue, FLinearColor& OutColor)
	{
		FString Value = InValue;
		Value.TrimStartAndEndInline();

		if (Value.Contains(TEXT("=")))
		{
			return OutColor.InitFromString(Value);
		}

		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3 && Parts.Num() != 4)
		{
			return false;
		}

		double Components[4] = { 0.0, 0.0, 0.0, 1.0 };
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			Components[Index] = FCString::Atod(*Parts[Index]);
		}

		OutColor = FLinearColor(
			static_cast<float>(Components[0]),
			static_cast<float>(Components[1]),
			static_cast<float>(Components[2]),
			static_cast<float>(Components[3]));
		return true;
	}

	bool ParseSettings(const FString& Params, FToonShadowColorCommandletSettings& OutSettings)
	{
		FString AssetPathValue;
		if (FParse::Value(*Params, TEXT("AssetPath="), AssetPathValue))
		{
			OutSettings.AssetPath = AssetPathValue;
		}

		FString BaseColorValue;
		if (FParse::Value(*Params, TEXT("BaseColor="), BaseColorValue) && !ParseColorString(BaseColorValue, OutSettings.BaseColor))
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Invalid BaseColor value: %s"), *BaseColorValue);
			return false;
		}

		FString ShadowColorValue;
		if (FParse::Value(*Params, TEXT("ShadowColor="), ShadowColorValue) && !ParseColorString(ShadowColorValue, OutSettings.ShadowColor))
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Invalid ShadowColor value: %s"), *ShadowColorValue);
			return false;
		}

		FParse::Value(*Params, TEXT("Metallic="), OutSettings.Metallic);
		FParse::Value(*Params, TEXT("Specular="), OutSettings.Specular);
		FParse::Value(*Params, TEXT("Roughness="), OutSettings.Roughness);

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

	UMaterialExpressionConstant3Vector* AddConstant3Vector(UMaterial* Material, const FLinearColor& Value, const int32 X, const int32 Y)
	{
		UMaterialExpressionConstant3Vector* Expression = Cast<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), X, Y));
		if (Expression)
		{
			Expression->Constant = Value;
		}
		return Expression;
	}

	UMaterialExpressionConstant* AddScalarConstant(UMaterial* Material, const float Value, const int32 X, const int32 Y)
	{
		UMaterialExpressionConstant* Expression = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), X, Y));
		if (Expression)
		{
			Expression->R = Value;
		}
		return Expression;
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

	bool ConfigureMaterial(UMaterial* Material, const FToonShadowColorCommandletSettings& Settings)
	{
		Material->Modify();
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->TwoSided = false;
		Material->bUseMaterialAttributes = false;
		Material->SetShadingModel(MSM_Toon);

		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

		UMaterialExpressionConstant3Vector* BaseColor = AddConstant3Vector(Material, Settings.BaseColor, -600, -300);
		UMaterialExpressionConstant3Vector* ShadowColor = AddConstant3Vector(Material, Settings.ShadowColor, -600, -100);
		UMaterialExpressionConstant* Metallic = AddScalarConstant(Material, Settings.Metallic, -600, 100);
		UMaterialExpressionConstant* Specular = AddScalarConstant(Material, Settings.Specular, -600, 300);
		UMaterialExpressionConstant* Roughness = AddScalarConstant(Material, Settings.Roughness, -600, 500);

		if (!BaseColor || !ShadowColor || !Metallic || !Specular || !Roughness)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to allocate one or more material expressions."));
			return false;
		}

		const bool bConnectedBaseColor = UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, TEXT(""), MP_BaseColor);
		const bool bConnectedShadowColor = UMaterialEditingLibrary::ConnectMaterialProperty(ShadowColor, TEXT(""), MP_ShadowColor);
		const bool bConnectedMetallic = UMaterialEditingLibrary::ConnectMaterialProperty(Metallic, TEXT(""), MP_Metallic);
		const bool bConnectedSpecular = UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);
		const bool bConnectedRoughness = UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		if (!(bConnectedBaseColor && bConnectedShadowColor && bConnectedMetallic && bConnectedSpecular && bConnectedRoughness))
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to connect one or more material properties."));
			return false;
		}

		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		Material->MarkPackageDirty();
		return true;
	}
}

UToonShadowColorCommandlet::UToonShadowColorCommandlet()
{
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	UseCommandletResultAsExitCode = true;

	HelpDescription = TEXT("Creates or updates a Toon smoke-test material that drives the Shadow Color pin.");
	HelpUsage = TEXT("UnrealEditor-Cmd.exe <Project.uproject> -run=ToonShadowColor -AssetPath=/Game/ToonTests/M_ToonShadowColorSmoke -ShadowColor=0.05,0.1,0.8");
	HelpParamNames = { TEXT("AssetPath"), TEXT("BaseColor"), TEXT("ShadowColor"), TEXT("Metallic"), TEXT("Specular"), TEXT("Roughness"), TEXT("NoRecompile"), TEXT("NoSave") };
	HelpParamDescriptions = {
		TEXT("Long package path or object path for the material asset."),
		TEXT("Base color as R,G,B[,A] or R=,G=,B= syntax."),
		TEXT("Shadow color as R,G,B[,A] or R=,G=,B= syntax."),
		TEXT("Metallic scalar value."),
		TEXT("Specular scalar value."),
		TEXT("Roughness scalar value."),
		TEXT("Skips material recompilation after graph edits."),
		TEXT("Skips package save after asset edits.")
	};
}

int32 UToonShadowColorCommandlet::Main(const FString& Params)
{
	FToonShadowColorCommandletSettings Settings;
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

	UE_LOG(LogTooncodex573Editor, Display, TEXT("Preparing Toon smoke-test material %s"), *ObjectPath);

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
		TEXT("Ready: %s | BaseColor=(%.3f, %.3f, %.3f) ShadowColor=(%.3f, %.3f, %.3f) Roughness=%.3f"),
		*ObjectPath,
		Settings.BaseColor.R,
		Settings.BaseColor.G,
		Settings.BaseColor.B,
		Settings.ShadowColor.R,
		Settings.ShadowColor.G,
		Settings.ShadowColor.B,
		Settings.Roughness);

	return 0;
}
