// Copyright Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
THIRD_PARTY_INCLUDES_START
#include <ktx.h>
THIRD_PARTY_INCLUDES_END

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
	static ktx_uint8_t* MallocKTXImageData(
		ktxTexture2** OutKTX2Texture,
		ktx_size_t* OutKTX2Offset,
		const TArray64<uint8>& RawBytes,
		const FglTFRuntimeImagesConfig& ImagesConfig,
		EPixelFormat& OutPixelFormat);
};
