// Copyright Roberto De Ioris

#include "glTFRuntimeKTX2FunctionLibrary.h"

#include "glTFRuntimeAsset.h"

bool UglTFRuntimeKTX2FunctionLibrary::IsAssetKTX2Compressed(UglTFRuntimeAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const TArray<FString> ExtensionsUsed = Asset->GetExtensionsUsed();
	return ExtensionsUsed.Contains(TEXT("KHR_texture_basisu"));
}
