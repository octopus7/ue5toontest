// Copyright Epic Games, Inc. All Rights Reserved.

#include "tooncodex573Editor.h"

#include "Bookmarks/IBookmarkTypeTools.h"
#include "Common/TcpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HighResScreenshot.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LevelEditor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SLevelViewport.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Styling/AppStyle.h"
#include "UnrealClient.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "Tooncodex573Editor"

DEFINE_LOG_CATEGORY(LogTooncodex573Editor);

namespace ToonViewportCaptureBridge
{
	static constexpr uint16 DefaultPort = 6767;
	static constexpr int32 DefaultResX = 512;
	static constexpr int32 DefaultResY = 512;
	static constexpr int32 DefaultBookmarkIndex = 1;
	static constexpr int32 MaxRequestBytes = 16 * 1024;
	static constexpr double ClientTimeoutSeconds = 5.0;
	static const FName ControlPanelTabId(TEXT("ToonViewportControlPanel"));

	struct FPendingClient
	{
		FSocket* Socket = nullptr;
		TArray<uint8> Buffer;
		double ConnectedAtSeconds = 0.0;
	};

	static FString BuildResponse(bool bOk, const FString& Message, const TFunctionRef<void(const TSharedRef<FJsonObject>&)>& AddFields)
	{
		const TSharedRef<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
		ResponseObject->SetBoolField(TEXT("ok"), bOk);
		ResponseObject->SetStringField(TEXT("message"), Message);
		AddFields(ResponseObject);

		FString ResponseText;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResponseText);
		FJsonSerializer::Serialize(ResponseObject, Writer);
		ResponseText.AppendChar(TEXT('\n'));
		return ResponseText;
	}

	static FString BuildSimpleResponse(bool bOk, const FString& Message)
	{
		return BuildResponse(bOk, Message, [](const TSharedRef<FJsonObject>&) {});
	}

	static FString BytesToString(const TArray<uint8>& Bytes, const int32 Length)
	{
		if (Length <= 0)
		{
			return FString();
		}

		FUTF8ToTCHAR Converter(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Length);
		return FString(Converter.Length(), Converter.Get());
	}

	static FString BytesToString(const TArray<uint8>& Bytes)
	{
		return BytesToString(Bytes, Bytes.Num());
	}

	static bool TryExtractLine(const TArray<uint8>& Buffer, FString& OutLine)
	{
		const int32 NewlineIndex = Buffer.IndexOfByKey(static_cast<uint8>('\n'));
		if (NewlineIndex == INDEX_NONE)
		{
			return false;
		}

		int32 Length = NewlineIndex;
		if ((Length > 0) && (Buffer[Length - 1] == static_cast<uint8>('\r')))
		{
			--Length;
		}

		OutLine = BytesToString(Buffer, Length);
		OutLine.TrimStartAndEndInline();
		return true;
	}

	static uint16 ResolveListenPort()
	{
		int32 RequestedPort = DefaultPort;
		if (FParse::Value(FCommandLine::Get(), TEXT("ToonViewportBridgePort="), RequestedPort))
		{
			if ((RequestedPort > 0) && (RequestedPort <= MAX_uint16))
			{
				return static_cast<uint16>(RequestedPort);
			}

			UE_LOG(LogTooncodex573Editor, Warning, TEXT("Ignoring invalid ToonViewportBridgePort value %d"), RequestedPort);
		}

		return DefaultPort;
	}

	static FString ResolveDefaultOutputDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT(".."), TEXT("ViewportCaptures")));
	}
}

class FTooncodex573EditorModule;

class SToonViewportControlPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SToonViewportControlPanel)
		: _OwnerModule(nullptr)
	{}
		SLATE_ARGUMENT(FTooncodex573EditorModule*, OwnerModule)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply HandleRecompileShaders();
	FReply HandleCaptureBookmarkOne();
	FReply HandleOpenOutputFolder();
	FText GetRecentShaderCompileText() const;
	void SetStatus(const FText& InStatus);

	FTooncodex573EditorModule* OwnerModule = nullptr;
	TSharedPtr<STextBlock> StatusTextBlock;
	FString OutputDirectory;
};

class FTooncodex573EditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ListenPort = ToonViewportCaptureBridge::ResolveListenPort();
		StartListener();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			ToonViewportCaptureBridge::ControlPanelTabId,
			FOnSpawnTab::CreateRaw(this, &FTooncodex573EditorModule::SpawnControlPanelTab))
			.SetDisplayName(LOCTEXT("ToonViewportControlPanelTitle", "Toon Capture"))
			.SetTooltipText(LOCTEXT("ToonViewportControlPanelTooltip", "Open the Toon capture control panel."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

		bPendingOpenControlTab = true;
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTooncodex573EditorModule::Tick), 0.1f);
	}

	virtual void ShutdownModule() override
	{
		if (TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
			TickHandle.Reset();
		}

		if (FSlateApplication::IsInitialized())
		{
			if (ControlPanelTab.IsValid())
			{
				ControlPanelTab.Pin()->RequestCloseTab();
			}

			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ToonViewportCaptureBridge::ControlPanelTabId);
		}

		StopListener();
	}

private:
	friend class SToonViewportControlPanel;

	bool Tick(float DeltaTime)
	{
		if (bPendingOpenControlTab && FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->TryInvokeTab(ToonViewportCaptureBridge::ControlPanelTabId);
			bPendingOpenControlTab = false;
		}

		if (ListenSocket == nullptr)
		{
			UpdateShaderRecompileState();
			return true;
		}

		AcceptPendingClients();
		ServicePendingClients();
		UpdateShaderRecompileState();
		return true;
	}

	void StartListener()
	{
		StopListener();

		const FIPv4Endpoint Endpoint(FIPv4Address::InternalLoopback, ListenPort);
		ListenSocket = FTcpSocketBuilder(TEXT("ToonViewportCaptureBridge"))
			.AsReusable()
			.AsNonBlocking()
			.BoundToEndpoint(Endpoint)
			.Listening(8);

		if (ListenSocket == nullptr)
		{
			UE_LOG(LogTooncodex573Editor, Error, TEXT("Failed to start viewport capture bridge on %s"), *Endpoint.ToString());
			return;
		}

		UE_LOG(LogTooncodex573Editor, Display, TEXT("Viewport capture bridge listening on %s"), *Endpoint.ToString());
	}

	void StopListener()
	{
		for (ToonViewportCaptureBridge::FPendingClient& Client : PendingClients)
		{
			DestroySocket(Client.Socket);
		}
		PendingClients.Reset();

		DestroySocket(ListenSocket);
	}

	void AcceptPendingClients()
	{
		bool bHasPendingConnection = false;
		while ((ListenSocket != nullptr) && ListenSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
		{
			FSocket* ClientSocket = ListenSocket->Accept(TEXT("ToonViewportCaptureClient"));
			if (ClientSocket == nullptr)
			{
				break;
			}

			ClientSocket->SetNonBlocking(true);

			ToonViewportCaptureBridge::FPendingClient& Client = PendingClients.AddDefaulted_GetRef();
			Client.Socket = ClientSocket;
			Client.ConnectedAtSeconds = FPlatformTime::Seconds();
		}
	}

	void ServicePendingClients()
	{
		const double Now = FPlatformTime::Seconds();
		for (int32 Index = PendingClients.Num() - 1; Index >= 0; --Index)
		{
			ToonViewportCaptureBridge::FPendingClient& Client = PendingClients[Index];
			FString ResponseText;
			bool bShouldClose = false;

			uint32 PendingDataSize = 0;
			while ((Client.Socket != nullptr) && Client.Socket->HasPendingData(PendingDataSize) && (PendingDataSize > 0))
			{
				const int32 BytesToRead = FMath::Min<int32>(static_cast<int32>(PendingDataSize), 4096);
				const int32 WriteOffset = Client.Buffer.Num();
				Client.Buffer.AddUninitialized(BytesToRead);

				int32 BytesRead = 0;
				if (!Client.Socket->Recv(Client.Buffer.GetData() + WriteOffset, BytesToRead, BytesRead))
				{
					ResponseText = ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("socket receive failed"));
					bShouldClose = true;
					break;
				}

				if (BytesRead <= 0)
				{
					Client.Buffer.SetNum(WriteOffset, EAllowShrinking::No);
					break;
				}

				Client.Buffer.SetNum(WriteOffset + BytesRead, EAllowShrinking::No);
				if (Client.Buffer.Num() > ToonViewportCaptureBridge::MaxRequestBytes)
				{
					ResponseText = ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("request exceeded 16384 bytes"));
					bShouldClose = true;
					break;
				}

				PendingDataSize = 0;
			}

			if (!bShouldClose)
			{
				FString RequestText;
				if (ToonViewportCaptureBridge::TryExtractLine(Client.Buffer, RequestText))
				{
					ResponseText = ProcessRequest(RequestText);
					bShouldClose = true;
				}
				else if ((Client.Socket == nullptr) || (Client.Socket->GetConnectionState() != SCS_Connected))
				{
					if (Client.Buffer.IsEmpty())
					{
						ResponseText = ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("empty request"));
					}
					else
					{
						FString BufferedRequest = ToonViewportCaptureBridge::BytesToString(Client.Buffer);
						BufferedRequest.TrimStartAndEndInline();
						ResponseText = ProcessRequest(BufferedRequest);
					}
					bShouldClose = true;
				}
				else if ((Now - Client.ConnectedAtSeconds) > ToonViewportCaptureBridge::ClientTimeoutSeconds)
				{
					ResponseText = ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("request timeout waiting for newline"));
					bShouldClose = true;
				}
			}

			if (bShouldClose)
			{
				SendResponse(Client.Socket, ResponseText);
				DestroySocket(Client.Socket);
				PendingClients.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			}
		}
	}

	FString ProcessRequest(const FString& RequestText)
	{
		if (RequestText.IsEmpty())
		{
			return ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("request body was empty"));
		}

		FString SanitizedRequest = RequestText;
		SanitizedRequest.TrimStartAndEndInline();
		if (!SanitizedRequest.IsEmpty() && (SanitizedRequest[0] == 0xFEFF))
		{
			SanitizedRequest.RightChopInline(1, EAllowShrinking::No);
		}

		TSharedPtr<FJsonObject> RequestObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SanitizedRequest);
		if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
		{
			return ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("request was not valid JSON"));
		}

		FString Command;
		if (!RequestObject->TryGetStringField(TEXT("command"), Command))
		{
			return ToonViewportCaptureBridge::BuildSimpleResponse(false, TEXT("missing command field"));
		}

		if (Command.Equals(TEXT("ping"), ESearchCase::IgnoreCase))
		{
			return ToonViewportCaptureBridge::BuildResponse(true, TEXT("pong"), [this](const TSharedRef<FJsonObject>& ResponseObject)
			{
				ResponseObject->SetNumberField(TEXT("port"), ListenPort);
			});
		}

		if (Command.Equals(TEXT("capture_viewport"), ESearchCase::IgnoreCase))
		{
			FString RequestedPath;
			RequestObject->TryGetStringField(TEXT("path"), RequestedPath);

			int32 ResX = 0;
			int32 ResY = 0;
			int32 BookmarkIndex = INDEX_NONE;
			RequestObject->TryGetNumberField(TEXT("res_x"), ResX);
			RequestObject->TryGetNumberField(TEXT("res_y"), ResY);
			RequestObject->TryGetNumberField(TEXT("bookmark_index"), BookmarkIndex);

			FString ResolvedPath;
			int32 CaptureResX = 0;
			int32 CaptureResY = 0;
			FString ErrorMessage;
			if (!CaptureViewportToFile(RequestedPath, ResX, ResY, BookmarkIndex, ResolvedPath, CaptureResX, CaptureResY, ErrorMessage))
			{
				return ToonViewportCaptureBridge::BuildSimpleResponse(false, ErrorMessage);
			}

			return ToonViewportCaptureBridge::BuildResponse(true, TEXT("capture queued"), [&ResolvedPath, CaptureResX, CaptureResY, BookmarkIndex](const TSharedRef<FJsonObject>& ResponseObject)
			{
				ResponseObject->SetStringField(TEXT("path"), ResolvedPath);
				ResponseObject->SetNumberField(TEXT("res_x"), CaptureResX);
				ResponseObject->SetNumberField(TEXT("res_y"), CaptureResY);
				if (BookmarkIndex != INDEX_NONE)
				{
					ResponseObject->SetNumberField(TEXT("bookmark_index"), BookmarkIndex);
				}
			});
		}

		return ToonViewportCaptureBridge::BuildSimpleResponse(false, FString::Printf(TEXT("unsupported command '%s'"), *Command));
	}

	bool RunBookmarkOneCapture(FString& OutResolvedPath, FString& OutError) const
	{
		int32 CaptureResX = 0;
		int32 CaptureResY = 0;
		return CaptureViewportToFile(
			FString(),
			ToonViewportCaptureBridge::DefaultResX,
			ToonViewportCaptureBridge::DefaultResY,
			ToonViewportCaptureBridge::DefaultBookmarkIndex,
			OutResolvedPath,
			CaptureResX,
			CaptureResY,
			OutError);
	}

	bool CaptureViewportToFile(const FString& RequestedPath, const int32 RequestedResX, const int32 RequestedResY, const int32 BookmarkIndex, FString& OutResolvedPath, int32& OutCaptureResX, int32& OutCaptureResY, FString& OutError) const
	{
		if ((RequestedResX < 0) || (RequestedResY < 0))
		{
			OutError = TEXT("res_x and res_y cannot be negative");
			return false;
		}

		const bool bHasExplicitResolution = (RequestedResX > 0) || (RequestedResY > 0);
		if (bHasExplicitResolution && ((RequestedResX <= 0) || (RequestedResY <= 0)))
		{
			OutError = TEXT("res_x and res_y must both be positive when provided");
			return false;
		}

		const TSharedPtr<SLevelViewport> LevelViewport = GetActiveLevelViewport(OutError);
		if (!LevelViewport.IsValid())
		{
			return false;
		}

		FLevelEditorViewportClient& LevelViewportClient = LevelViewport->GetLevelViewportClient();
		if (!ApplyBookmarkIfRequested(LevelViewportClient, BookmarkIndex, OutError))
		{
			return false;
		}

		if (!LevelViewportClient.IsPerspective())
		{
			OutError = TEXT("active level viewport must be perspective");
			return false;
		}

		OutCaptureResX = bHasExplicitResolution ? RequestedResX : ToonViewportCaptureBridge::DefaultResX;
		OutCaptureResY = bHasExplicitResolution ? RequestedResY : ToonViewportCaptureBridge::DefaultResY;
		OutResolvedPath = ResolveOutputPath(RequestedPath);

		const FString OutputDirectory = FPaths::GetPath(OutResolvedPath);
		if (!OutputDirectory.IsEmpty() && !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(TEXT("failed to create output directory '%s'"), *OutputDirectory);
			return false;
		}

		FViewport* const ActiveViewport = LevelViewport->GetActiveViewport();
		if (ActiveViewport == nullptr)
		{
			OutError = TEXT("active level viewport has no active FViewport");
			return false;
		}

		FHighResScreenshotConfig& ScreenshotConfig = GetHighResScreenshotConfig();
		ScreenshotConfig.SetResolution(OutCaptureResX, OutCaptureResY);
		ScreenshotConfig.SetFilename(OutResolvedPath);
		ScreenshotConfig.SetMaskEnabled(false);
		ScreenshotConfig.SetHDRCapture(false);

		ActiveViewport->TakeHighResScreenShot();

		FString BookmarkSuffix;
		if (BookmarkIndex != INDEX_NONE)
		{
			BookmarkSuffix = FString::Printf(TEXT(" using bookmark %d"), BookmarkIndex);
		}

		UE_LOG(LogTooncodex573Editor, Display, TEXT("Queued viewport capture to '%s' at %dx%d%s"), *OutResolvedPath, OutCaptureResX, OutCaptureResY, *BookmarkSuffix);
		return true;
	}

	bool ApplyBookmarkIfRequested(FLevelEditorViewportClient& LevelViewportClient, const int32 BookmarkIndex, FString& OutError) const
	{
		if (BookmarkIndex == INDEX_NONE)
		{
			return true;
		}

		if ((BookmarkIndex < 0) || (BookmarkIndex >= static_cast<int32>(AWorldSettings::NumMappedBookmarks)))
		{
			OutError = FString::Printf(TEXT("bookmark_index must be between 0 and %d"), static_cast<int32>(AWorldSettings::NumMappedBookmarks) - 1);
			return false;
		}

		if (!IBookmarkTypeTools::Get().CheckBookmark(BookmarkIndex, &LevelViewportClient))
		{
			OutError = FString::Printf(TEXT("bookmark %d is not set in the active level"), BookmarkIndex);
			return false;
		}

		IBookmarkTypeTools::Get().JumpToBookmark(BookmarkIndex, TSharedPtr<FBookmarkBaseJumpToSettings>(), &LevelViewportClient);
		if (GEditor != nullptr)
		{
			GEditor->RedrawAllViewports(false);
		}

		return true;
	}

	TSharedPtr<SLevelViewport> GetActiveLevelViewport(FString& OutError) const
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
		{
			OutError = TEXT("LevelEditor module is not loaded");
			return nullptr;
		}

		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		const TSharedPtr<SLevelViewport> LevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
		if (!LevelViewport.IsValid())
		{
			OutError = TEXT("no active level viewport");
			return nullptr;
		}

		return LevelViewport;
	}

	TSharedRef<SDockTab> SpawnControlPanelTab(const FSpawnTabArgs& Args)
	{
		const TSharedRef<SToonViewportControlPanel> ControlPanel =
			SNew(SToonViewportControlPanel)
			.OwnerModule(this);

		ControlPanelWidget = ControlPanel;

		return SAssignNew(ControlPanelTab, SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				ControlPanel
			];
	}

	void BeginShaderRecompileTiming()
	{
		bAwaitingShaderRecompileCompletion = true;
		bObservedActiveShaderCompile = (GShaderCompilingManager != nullptr) && GShaderCompilingManager->IsCompiling();
		ShaderRecompileStartSeconds = FPlatformTime::Seconds();
		InvalidateControlPanel();
	}

	void CancelShaderRecompileTiming()
	{
		bAwaitingShaderRecompileCompletion = false;
		bObservedActiveShaderCompile = false;
		ShaderRecompileStartSeconds = 0.0;
		InvalidateControlPanel();
	}

	FText GetRecentShaderCompileText() const
	{
		if (bAwaitingShaderRecompileCompletion)
		{
			return LOCTEXT("ToonCaptureRecentShaderCompileRunning", "최근 소요시간: 측정 중...");
		}

		if (LastShaderRecompileDurationSeconds < 0.0)
		{
			return LOCTEXT("ToonCaptureRecentShaderCompileNone", "최근 소요시간: 없음");
		}

		FNumberFormattingOptions SecondsFormat;
		SecondsFormat.SetMinimumFractionalDigits(1);
		SecondsFormat.SetMaximumFractionalDigits(1);

		if (LastShaderRecompileDurationSeconds >= 60.0)
		{
			const int32 WholeMinutes = FMath::FloorToInt(LastShaderRecompileDurationSeconds / 60.0);
			const double RemainingSeconds = LastShaderRecompileDurationSeconds - (static_cast<double>(WholeMinutes) * 60.0);
			return FText::Format(
				LOCTEXT("ToonCaptureRecentShaderCompileMinutes", "최근 소요시간: {0}분 {1}초"),
				FText::AsNumber(WholeMinutes),
				FText::AsNumber(RemainingSeconds, &SecondsFormat));
		}

		return FText::Format(
			LOCTEXT("ToonCaptureRecentShaderCompileSeconds", "최근 소요시간: {0}초"),
			FText::AsNumber(LastShaderRecompileDurationSeconds, &SecondsFormat));
	}

	void UpdateShaderRecompileState()
	{
		if (!bAwaitingShaderRecompileCompletion)
		{
			return;
		}

		const bool bIsCompiling = (GShaderCompilingManager != nullptr) && GShaderCompilingManager->IsCompiling();
		if (bIsCompiling)
		{
			bObservedActiveShaderCompile = true;
			return;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		const bool bTimedOutWaitingForWork = !bObservedActiveShaderCompile && ((NowSeconds - ShaderRecompileStartSeconds) >= 0.25);
		if (!bObservedActiveShaderCompile && !bTimedOutWaitingForWork)
		{
			return;
		}

		LastShaderRecompileDurationSeconds = FMath::Max(0.0, NowSeconds - ShaderRecompileStartSeconds);
		bAwaitingShaderRecompileCompletion = false;
		bObservedActiveShaderCompile = false;
		ShaderRecompileStartSeconds = 0.0;
		InvalidateControlPanel();
	}

	void InvalidateControlPanel() const
	{
		if (ControlPanelWidget.IsValid())
		{
			ControlPanelWidget.Pin()->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	FString ResolveOutputPath(const FString& RequestedPath) const
	{
		FString OutputPath = RequestedPath;
		if (OutputPath.IsEmpty())
		{
			OutputPath = FPaths::Combine(
				ToonViewportCaptureBridge::ResolveDefaultOutputDirectory(),
				FString::Printf(TEXT("Bookmark1Capture_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))
			);
		}
		else
		{
			OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
		}

		if (FPaths::GetExtension(OutputPath).IsEmpty())
		{
			OutputPath.Append(TEXT(".png"));
		}

		return FPaths::ConvertRelativePathToFull(OutputPath);
	}

	void SendResponse(FSocket* ClientSocket, const FString& ResponseText) const
	{
		if ((ClientSocket == nullptr) || ResponseText.IsEmpty())
		{
			return;
		}

		FTCHARToUTF8 Converter(*ResponseText);
		int32 BytesSent = 0;
		ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);
	}

	static void DestroySocket(FSocket*& Socket)
	{
		if (Socket == nullptr)
		{
			return;
		}

		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}

private:
	FSocket* ListenSocket = nullptr;
	uint16 ListenPort = ToonViewportCaptureBridge::DefaultPort;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<ToonViewportCaptureBridge::FPendingClient> PendingClients;
	TWeakPtr<SDockTab> ControlPanelTab;
	TWeakPtr<SToonViewportControlPanel> ControlPanelWidget;
	bool bPendingOpenControlTab = false;
	bool bAwaitingShaderRecompileCompletion = false;
	bool bObservedActiveShaderCompile = false;
	double ShaderRecompileStartSeconds = 0.0;
	double LastShaderRecompileDurationSeconds = -1.0;
};

void SToonViewportControlPanel::Construct(const FArguments& InArgs)
{
	OwnerModule = InArgs._OwnerModule;
	OutputDirectory = ToonViewportCaptureBridge::ResolveDefaultOutputDirectory();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ToonCaptureHeader", "Bookmark 1 Capture"))
				.Font(FAppStyle::GetFontStyle(TEXT("HeadingMedium")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ToonCaptureDescription", "Moves the active viewport to bookmark 1, then saves a 512x512 PNG with an offscreen scene capture."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.HeightOverride(36.0f)
					.MinDesiredWidth(130.0f)
					[
						SNew(SButton)
						.OnClicked(this, &SToonViewportControlPanel::HandleRecompileShaders)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ToonCaptureRecompileShadersButton", "Recompile Shaders"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.HeightOverride(36.0f)
					[
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
						.OnClicked(this, &SToonViewportControlPanel::HandleCaptureBookmarkOne)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ToonCaptureButton", "Camera 1 -> Capture 512x512"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.HeightOverride(36.0f)
					.MinDesiredWidth(110.0f)
					[
						SNew(SButton)
						.OnClicked(this, &SToonViewportControlPanel::HandleOpenOutputFolder)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ToonCaptureOpenFolderButton", "Open Folder"))
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SToonViewportControlPanel::GetRecentShaderCompileText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ToonCaptureStatusIdle", "Ready."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::Format(LOCTEXT("ToonCaptureOutputDirectory", "Output folder: {0}"), FText::FromString(OutputDirectory)))
			]
		]
	];
}

FReply SToonViewportControlPanel::HandleRecompileShaders()
{
	UWorld* World = nullptr;
	if (GEditor != nullptr)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (OwnerModule != nullptr)
	{
		OwnerModule->BeginShaderRecompileTiming();
	}

	bool bHandled = false;
	if (GEditor != nullptr)
	{
		bHandled = GEditor->Exec(World, TEXT("RecompileShaders Changed"), *GLog);
	}

	if (!bHandled && (GEngine != nullptr))
	{
		bHandled = GEngine->Exec(World, TEXT("RecompileShaders Changed"), *GLog);
	}

	if (!bHandled)
	{
		if (OwnerModule != nullptr)
		{
			OwnerModule->CancelShaderRecompileTiming();
		}

		SetStatus(LOCTEXT("ToonCaptureRecompileShadersFailed", "Failed to start shader recompilation."));
		return FReply::Handled();
	}

	SetStatus(LOCTEXT("ToonCaptureRecompileShadersStarted", "Started shader recompilation for changed shaders."));
	return FReply::Handled();
}

FReply SToonViewportControlPanel::HandleCaptureBookmarkOne()
{
	if (OwnerModule == nullptr)
	{
		SetStatus(LOCTEXT("ToonCaptureNoModule", "Capture module is not available."));
		return FReply::Handled();
	}

	FString OutputPath;
	FString ErrorMessage;
	if (!OwnerModule->RunBookmarkOneCapture(OutputPath, ErrorMessage))
	{
		SetStatus(FText::FromString(ErrorMessage));
		return FReply::Handled();
	}

	SetStatus(FText::Format(LOCTEXT("ToonCaptureQueued", "Queued: {0}"), FText::FromString(OutputPath)));
	return FReply::Handled();
}

FReply SToonViewportControlPanel::HandleOpenOutputFolder()
{
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	FPlatformProcess::ExploreFolder(*OutputDirectory);
	return FReply::Handled();
}

FText SToonViewportControlPanel::GetRecentShaderCompileText() const
{
	if (OwnerModule != nullptr)
	{
		return OwnerModule->GetRecentShaderCompileText();
	}

	return LOCTEXT("ToonCaptureRecentShaderCompileUnavailable", "최근 소요시간: 없음");
}

void SToonViewportControlPanel::SetStatus(const FText& InStatus)
{
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(InStatus);
	}
}

IMPLEMENT_MODULE(FTooncodex573EditorModule, tooncodex573Editor)

#undef LOCTEXT_NAMESPACE
