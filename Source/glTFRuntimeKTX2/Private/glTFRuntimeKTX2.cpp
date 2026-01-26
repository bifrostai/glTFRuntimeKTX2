// Copyright Roberto De Ioris

#include "glTFRuntimeKTX2.h"

#include "glTFRuntimeParser.h"

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

ktx_uint8_t* FglTFRuntimeKTX2Module::MallocKTXImageData(
	ktxTexture2** OutKTX2Texture,
	ktx_size_t* OutKTX2Offset,
	const TArray64<uint8>& RawBytes,
	const FglTFRuntimeImagesConfig& ImagesConfig,
	EPixelFormat& OutPixelFormat)
{
	// Create KTX2 texture from memory
	KTX_error_code KTXResult = ktxTexture2_CreateFromMemory(
		RawBytes.GetData(),
		ktx_size_t(RawBytes.Num()),
		KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
		OutKTX2Texture);

	if (KTXResult != KTX_SUCCESS)
	{
		return nullptr;
	}

	// Determine transcoding format from ForcePixelFormat if set, otherwise use RGBA32
	ktx_transcode_fmt_e TranscodeFormat = KTX_TTF_RGBA32;
	EPixelFormat TargetPixelFormat = EPixelFormat::PF_B8G8R8A8;

	if (ImagesConfig.ForcePixelFormat != EPixelFormat::PF_Unknown)
	{
		EPixelFormat RequestedFormat = ImagesConfig.ForcePixelFormat;
		ktx_transcode_fmt_e RequestedTranscodeFormat =
			static_cast<ktx_transcode_fmt_e>(FglTFRuntimeKTX2Module::GetKTXFormatFromUnrealPixelFormat(RequestedFormat));
		if (RequestedTranscodeFormat != static_cast<ktx_transcode_fmt_e>(0x7fffffff)) // KTX_TTF_NOSELECTION
		{
			TranscodeFormat = RequestedTranscodeFormat;
			TargetPixelFormat = RequestedFormat;
		}
	}

	// Transcode if needed
	if (ktxTexture2_NeedsTranscoding(*OutKTX2Texture))
	{
		KTXResult = ktxTexture2_TranscodeBasis(*OutKTX2Texture, TranscodeFormat, 0);
		if (KTXResult != KTX_SUCCESS)
		{
			ktxTexture_Destroy(ktxTexture(*OutKTX2Texture));
			*OutKTX2Texture = nullptr;
			return nullptr;
		}
	}

	// Get image offset for mip level 0
	KTXResult = ktxTexture_GetImageOffset(ktxTexture(*OutKTX2Texture), 0, 0, 0, OutKTX2Offset);
	if (KTXResult != KTX_SUCCESS)
	{
		ktxTexture_Destroy(ktxTexture(*OutKTX2Texture));
		*OutKTX2Texture = nullptr;
		return nullptr;
	}

	// Set output dimensions and format
	OutPixelFormat = TargetPixelFormat;

	// Get image data pointer
	ktx_uint8_t* KTX2ImageData = ktxTexture_GetData(ktxTexture(*OutKTX2Texture)) + *OutKTX2Offset;
	const int64 ImageSize = ktxTexture_GetImageSize(ktxTexture(*OutKTX2Texture), 0);

	// Only perform RGBA byte swapping for B8G8R8A8 format
	if (OutPixelFormat == EPixelFormat::PF_B8G8R8A8)
	{
		for (int64 IndexR = 0; IndexR < ImageSize; IndexR += 4)
		{
			Swap(KTX2ImageData[IndexR], KTX2ImageData[IndexR + 2]);
		}
	}

	return KTX2ImageData;
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

		ktxTexture2* KTX2Texture = nullptr;
		ktx_size_t KTX2Offset = 0;
		EPixelFormat OutPixelFormat = EPixelFormat::PF_Unknown;
		ktx_uint8_t* const KTX2ImageData = MallocKTXImageData(&KTX2Texture, &KTX2Offset, CompressedBytes, ImagesConfig, OutPixelFormat);

		if (KTX2ImageData == nullptr)
		{
			Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to load KTX2 texture");
			ktxTexture_Destroy(ktxTexture(KTX2Texture));
			return;
		}

		if (KTX2Texture->numLevels > 0)
		{
			int32 MipWidth = KTX2Texture->baseWidth;
			int32 MipHeight = KTX2Texture->baseHeight;

			int64 ImageSize = ktxTexture_GetImageSize(ktxTexture(KTX2Texture), 0);
			FglTFRuntimeMipMap Mip0(
				TextureIndex,
				OutPixelFormat,
				MipWidth,
				MipHeight,
				TArray64<uint8>(KTX2ImageData, ImageSize)
			);
			Mips.Add(MoveTemp(Mip0));

			// setup the rest of the MIPs
			for (uint32 MipIndex = 1; MipIndex < KTX2Texture->numLevels; MipIndex++)
			{
				const ktx_error_code_e KTXResult = ktxTexture_GetImageOffset(ktxTexture(KTX2Texture), MipIndex, 0, 0, &KTX2Offset);
				if (KTXResult != KTX_SUCCESS)
				{
					ktxTexture_Destroy(ktxTexture(KTX2Texture));
					Parser->AddError("FglTFRuntimeKTX2Module::StartupModule()", "Unable to get KTX2 texture offset for mips generation");
					return;
				}

				ktx_uint8_t* const KTX2MipsImageData = ktxTexture_GetData(ktxTexture(KTX2Texture)) + KTX2Offset;

				FglTFRuntimeMipMap Mip(TextureIndex);
				MipWidth = FMath::Max(MipWidth / 2, 1);
				MipHeight = FMath::Max(MipHeight / 2, 1);
				Mip.Width = MipWidth;
				Mip.Height = MipHeight;
				Mip.PixelFormat = OutPixelFormat;
				ImageSize = ktxTexture_GetImageSize(ktxTexture(KTX2Texture), MipIndex);

				if (OutPixelFormat == EPixelFormat::PF_B8G8R8A8)
				{
					for (int64 IndexR = 0; IndexR < ImageSize; IndexR += 4)
					{
						Swap(KTX2MipsImageData[IndexR], KTX2MipsImageData[IndexR + 2]);
					}
				}

				Mip.Pixels.Append(KTX2MipsImageData, ImageSize);
				Mips.Add(MoveTemp(Mip));
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

		ktxTexture2* KTX2Texture = nullptr;
		ktx_size_t KTX2Offset = 0;
		ktx_uint8_t* KTX2ImageData = MallocKTXImageData(&KTX2Texture, &KTX2Offset, CompressedPixels, ImagesConfig, PixelFormat);

		if (KTX2ImageData == nullptr)
		{
			Parser->AddError("FglTFRuntimeKTX2Module::OnTexturePixels()", "Unable to load KTX2 texture");
			ktxTexture_Destroy(ktxTexture(KTX2Texture));
			return;
		}

		Width = KTX2Texture->baseWidth;
		Height = KTX2Texture->baseHeight;
		const int64 ImageSize = ktxTexture_GetImageSize(ktxTexture(KTX2Texture), 0);

		// Mark completion of transcoding and cleanup
		UncompressedPixels.Append(KTX2ImageData, ImageSize);
		ktxTexture_Destroy(ktxTexture(KTX2Texture));
	});
}

void FglTFRuntimeKTX2Module::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FglTFRuntimeKTX2Module, glTFRuntimeKTX2)
