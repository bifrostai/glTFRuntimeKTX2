// Copyright Roberto De Ioris

#include "glTFRuntimeKTX2.h"

#include "glTFRuntimeParser.h"
THIRD_PARTY_INCLUDES_START
#include <ktx.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "FglTFRuntimeKTX2Module"

// Mapping table is provided by the class as a private static helper
const FglTFRuntimeKTX2Module::FFormatPair* FglTFRuntimeKTX2Module::GetMappingTable(int32& OutNumEntries)
{
	static const FFormatPair Mappings[] = {
		// Uncompressed formats
		{ EPixelFormat::PF_B8G8R8A8, (int32)KTX_TTF_RGBA32 },

		// BC/DXT formats (desktop)
		{ EPixelFormat::PF_DXT1, (int32)KTX_TTF_BC1_RGB },
		{ EPixelFormat::PF_DXT5, (int32)KTX_TTF_BC3_RGBA },
		{ EPixelFormat::PF_BC4, (int32)KTX_TTF_BC4_R },
		{ EPixelFormat::PF_BC5, (int32)KTX_TTF_BC5_RG },
		{ EPixelFormat::PF_BC7, (int32)KTX_TTF_BC7_RGBA },
	};

	OutNumEntries = sizeof(Mappings) / sizeof(Mappings[0]);
	return Mappings;
}

int32 FglTFRuntimeKTX2Module::GetKTXFormatFromUnrealPixelFormat(EPixelFormat UnrealFormat)
{
	// Ensure the Unreal pixel format is supported on this platform
	if (UnrealFormat <= EPixelFormat::PF_Unknown || UnrealFormat >= EPixelFormat::PF_MAX)
	{
		return static_cast<int32>(0x7fffffff);
	}
	if (!GPixelFormats[UnrealFormat].Supported)
	{
		return static_cast<int32>(0x7fffffff);
	}

	int32 Count = 0;
	const FFormatPair* Table = GetMappingTable(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FFormatPair& Pair = Table[i];
		if (Pair.Unreal == UnrealFormat)
		{
			return Pair.Ktx;
		}
	}
	return static_cast<int32>(0x7fffffff); // KTX_TTF_NOSELECTION
}

EPixelFormat FglTFRuntimeKTX2Module::GetUnrealPixelFormatFromKTXFormat(int32 KTXFormat)
{
	int32 Count = 0;
	const FFormatPair* Table = GetMappingTable(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FFormatPair& Pair = Table[i];
		if (Pair.Ktx == KTXFormat)
		{
			// Only return formats supported on this platform
			if (Pair.Unreal > EPixelFormat::PF_Unknown && Pair.Unreal < EPixelFormat::PF_MAX && GPixelFormats[Pair.Unreal].Supported)
			{
				return Pair.Unreal;
			}
		}
	}
	return EPixelFormat::PF_Unknown;
}

void FglTFRuntimeKTX2Module::StartupModule()
{
	// extract the right ImageIndex
	FglTFRuntimeParser::OnTextureImageIndex.AddLambda([](TSharedRef<FglTFRuntimeParser> Parser, TSharedRef<FJsonObject> JsonTextureObject, int64& ImageIndex) {
		// check for used extensions
		if (!Parser->ExtensionsUsed.Contains("KHR_texture_basisu"))
		{
			return;
		}

		ImageIndex = Parser->GetJsonExtensionObjectIndex(JsonTextureObject, "KHR_texture_basisu", "source", INDEX_NONE);
	});

	// check if we have mips
	FglTFRuntimeParser::OnTextureMips.AddLambda([](TSharedRef<FglTFRuntimeParser> Parser, const int32 TextureIndex, TSharedRef<FJsonObject> JsonTextureObject, TSharedRef<FJsonObject> JsonImageObject, const TArray64<uint8>& CompressedBytes, TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig) {
		// skip if already processed
		if (Mips.Num() > 0)
		{
			return;
		}

		FString MimeType;

		if (!ImagesConfig.bForceAutoDetect)
		{

			// check for used extensions
			if (!Parser->ExtensionsUsed.Contains("KHR_texture_basisu"))
			{
				return;
			}

			MimeType = Parser->GetJsonObjectString(JsonImageObject, "mimeType", "");
			// skip non ktx2 mimeType (can be empty)
			if (MimeType != "" && MimeType != "image/ktx2")
			{
				return;
			}
		}

		// Determine transcoding format from ForcePixelFormat
		if (ImagesConfig.ForcePixelFormat == EPixelFormat::PF_Unknown)
		{
			return;
		}

		ktx_transcode_fmt_e TranscodeFormat = static_cast<ktx_transcode_fmt_e>(FglTFRuntimeKTX2Module::GetKTXFormatFromUnrealPixelFormat(ImagesConfig.ForcePixelFormat));
		// If format not supported, skip
		if (TranscodeFormat == static_cast<ktx_transcode_fmt_e>(0x7fffffff)) // KTX_TTF_NOSELECTION
		{
			return;
		}

		// make sure its a format we can transcode
		if (FglTFRuntimeKTX2Module::GetUnrealPixelFormatFromKTXFormat(static_cast<int32>(TranscodeFormat)) == EPixelFormat::PF_Unknown)
		{
			Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unsupported KTX2 transcoding format");
			return;
		}

		ktxTexture2* KTX2Texture = nullptr;
		KTX_error_code KTXResult = KTX_UNSUPPORTED_TEXTURE_TYPE;
		ktx_size_t KTX2Offset = 0;

		KTXResult = ktxTexture2_CreateFromMemory(CompressedBytes.GetData(), CompressedBytes.Num(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &KTX2Texture);
		if (KTXResult != KTX_SUCCESS)
		{
			if (KTXResult != KTX_UNKNOWN_FILE_FORMAT)
			{
				Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to load KTX2 texture");
			}
			return;
		}

		if (KTX2Texture->numLevels > 0)
		{
			EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
			if (ktxTexture2_NeedsTranscoding(KTX2Texture))
			{
				KTXResult = ktxTexture2_TranscodeBasis(KTX2Texture, TranscodeFormat, 0);
				if (KTXResult != KTX_SUCCESS)
				{
					ktxTexture_Destroy(ktxTexture(KTX2Texture));
					Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to transcode KTX2 texture");
					return;
				}
				// Use the format we transcoded to (from ForcePixelFormat or CVAR)
				if (ImagesConfig.ForcePixelFormat != EPixelFormat::PF_Unknown)
				{
					PixelFormat = ImagesConfig.ForcePixelFormat;
				}
				else
				{
					PixelFormat = FglTFRuntimeKTX2Module::GetUnrealPixelFormatFromKTXFormat(static_cast<int32>(TranscodeFormat));
					if (PixelFormat == EPixelFormat::PF_Unknown)
					{
						ktxTexture_Destroy(ktxTexture(KTX2Texture));
						Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unsupported KTX2 transcoded pixel format");
						return;
					}
				}
			}
			else
			{
				// This is highly unlikely that we can use the KTX2 as-is. We'll implement it in the future
				// if we need to with specific use cases which there isn't right now
				ktxTexture_Destroy(ktxTexture(KTX2Texture));
				Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "KTX2 texture does not need transcoding, unsupported path");
				return;
			}
			int32 MipWidth = KTX2Texture->baseWidth;
			int32 MipHeight = KTX2Texture->baseHeight;

			for (uint32 MipIndex = 0; MipIndex < KTX2Texture->numLevels; MipIndex++)
			{
				KTXResult = ktxTexture_GetImageOffset(ktxTexture(KTX2Texture), MipIndex, 0, 0, &KTX2Offset);
				if (KTXResult != KTX_SUCCESS)
				{
					ktxTexture_Destroy(ktxTexture(KTX2Texture));
					Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to get KTX2 texture offset");
					return;
				}

				ktx_uint8_t* const KTX2ImageData = ktxTexture_GetData(ktxTexture(KTX2Texture)) + KTX2Offset;
				const int64 ImageSize = ktxTexture_GetImageSize(ktxTexture(KTX2Texture), MipIndex);

				FglTFRuntimeMipMap Mip(TextureIndex);
				Mip.Width = MipWidth;
				Mip.Height = MipHeight;
				Mip.PixelFormat = PixelFormat;

				if (PixelFormat == EPixelFormat::PF_B8G8R8A8)
				{
					for (int64 IndexR = 0; IndexR < ImageSize; IndexR += 4)
					{
						Swap(KTX2ImageData[IndexR], KTX2ImageData[IndexR + 2]);
					}
				}

				Mip.Pixels.Append(KTX2ImageData, ImageSize);

				Mips.Add(MoveTemp(Mip));

				MipWidth = FMath::Max(MipWidth / 2, 1);
				MipHeight = FMath::Max(MipHeight / 2, 1);
			}
		}

		ktxTexture_Destroy(ktxTexture(KTX2Texture));
	});

	// extract ImagePixels
	FglTFRuntimeParser::OnTexturePixels.AddLambda([](TSharedRef<FglTFRuntimeParser> Parser, TSharedRef<FJsonObject> JsonImageObject, const TArray64<uint8>& CompressedPixels, int32& Width, int32& Height, EPixelFormat& PixelFormat, TArray64<uint8>& UncompressedPixels, const FglTFRuntimeImagesConfig& ImagesConfig) {
		// skip if already processed
		if (UncompressedPixels.Num() > 0)
		{
			return;
		}

		// check for used extensions
		if (!Parser->ExtensionsUsed.Contains("KHR_texture_basisu"))
		{
			return;
		}

		const FString MimeType = Parser->GetJsonObjectString(JsonImageObject, "mimeType", "");
		// skip non ktx2 mimeType (can be empty)
		if (MimeType != "" && MimeType != "image/ktx2")
		{
			return;
		}

		ktxTexture2* KTX2Texture;
		KTX_error_code KTXResult;
		ktx_size_t KTX2Offset;

		KTXResult = ktxTexture2_CreateFromMemory(CompressedPixels.GetData(), CompressedPixels.Num(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &KTX2Texture);
		if (KTXResult != KTX_SUCCESS)
		{
			if (MimeType != "" || (MimeType == "" && KTXResult != KTX_UNKNOWN_FILE_FORMAT))
			{
				Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to load KTX2 texture");
			}
			return;
		}

		if (KTX2Texture->numLevels > 0)
		{
			if (ktxTexture2_NeedsTranscoding(KTX2Texture))
			{
				KTXResult = ktxTexture2_TranscodeBasis(KTX2Texture, KTX_TTF_RGBA32, 0);
				if (KTXResult != KTX_SUCCESS)
				{
					ktxTexture_Destroy(ktxTexture(KTX2Texture));
					Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to transcode KTX2 texture");
					return;
				}
			}
			KTXResult = ktxTexture_GetImageOffset(ktxTexture(KTX2Texture), 0, 0, 0, &KTX2Offset);
			if (KTXResult != KTX_SUCCESS)
			{
				ktxTexture_Destroy(ktxTexture(KTX2Texture));
				Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to get KTX2 texture offset");
				return;
			}

			Width = KTX2Texture->baseWidth;
			Height = KTX2Texture->baseHeight;
			PixelFormat = EPixelFormat::PF_B8G8R8A8;

			ktx_uint8_t* KTX2ImageData = ktxTexture_GetData(ktxTexture(KTX2Texture)) + KTX2Offset;

			const int64 ImageSize = Width * Height * 4;

			for (int64 IndexR = 0; IndexR < ImageSize; IndexR += 4)
			{
				Swap(KTX2ImageData[IndexR], KTX2ImageData[IndexR + 2]);
			}
			UncompressedPixels.Append(KTX2ImageData, ImageSize);
		}

		ktxTexture_Destroy(ktxTexture(KTX2Texture));
	});
}

void FglTFRuntimeKTX2Module::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FglTFRuntimeKTX2Module, glTFRuntimeKTX2)