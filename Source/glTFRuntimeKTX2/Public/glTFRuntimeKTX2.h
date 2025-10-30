// Copyright Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FglTFRuntimeKTX2Module : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static int32 GetKTXFormatFromUnrealPixelFormat(EPixelFormat UnrealFormat);
	static EPixelFormat GetUnrealPixelFormatFromKTXFormat(int32 KTXFormat);

private:
	struct FFormatPair
	{
		EPixelFormat Unreal;
		int32 Ktx; // stored as int32 to avoid exposing ktx headers
	};

	static const FFormatPair* GetMappingTable(int32& OutNumEntries);
};
