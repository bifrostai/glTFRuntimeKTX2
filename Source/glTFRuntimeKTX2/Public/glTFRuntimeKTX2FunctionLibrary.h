// Copyright Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeKTX2FunctionLibrary.generated.h"

/**
 * Function library for glTFRuntimeKTX2 utilities
 */
UCLASS()
class GLTFRUNTIMEKTX2_API UglTFRuntimeKTX2FunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Checks if a glTF asset uses KTX2 compressed textures
	 * @param Asset The glTF runtime asset to check
	 * @return True if the asset uses KTX2 compression (indicated by KHR_texture_basisu extension), false otherwise
	 */
	static bool IsAssetKTX2Compressed(UglTFRuntimeAsset* Asset);
};

