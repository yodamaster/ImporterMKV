#include "Importer.h"
// reviewed 0.3.8
static void avlog_cb(void *, int level, const char * szFmt, va_list varg)
{
	char logbuf[2000];
	vsnprintf(logbuf, sizeof(logbuf), szFmt, varg);
	logbuf[sizeof(logbuf) - 1] = '\0';

	OutputDebugStringA(logbuf);
}

PREMPLUGENTRY DllExport xImportEntry(csSDK_int32 selector, imStdParms *stdParms, void *param1, void *param2)
{
	prMALError	result = imUnsupported;

	switch (selector)
	{
	case imInit:
		result = SDKInit(stdParms, reinterpret_cast<imImportInfoRec*>(param1));
		break;

	case imGetSupports8:
		result = malSupports8;
		break;
	
	case imGetIndFormat:
		result = SDKGetIndFormat(stdParms, reinterpret_cast<csSDK_size_t>(param1), reinterpret_cast<imIndFormatRec*>(param2));
		break;

	case imGetPrefs8:
		result = SDKGetPrefs8(stdParms, reinterpret_cast<imFileAccessRec8*>(param1), reinterpret_cast<imGetPrefsRec*>(param2));
		break;

	case imOpenFile8:
		result = SDKOpenFile8(stdParms, reinterpret_cast<imFileRef*>(param1), reinterpret_cast<imFileOpenRec8*>(param2));
		break;

	case imQuietFile:
		result = SDKQuietFile(stdParms, reinterpret_cast<imFileRef*>(param1), param2);
		break;

	case imCloseFile:
		result = SDKCloseFile(stdParms, reinterpret_cast<imFileRef*>(param1), param2);
		break;

	case imGetInfo8:
		result = SDKGetInfo8(stdParms, reinterpret_cast<imFileAccessRec8*>(param1),	reinterpret_cast<imFileInfoRec8*>(param2));
		break;

	case imGetIndPixelFormat:
		result = SDKGetIndPixelFormat(stdParms,	reinterpret_cast<csSDK_size_t>(param1),	reinterpret_cast<imIndPixelFormatRec*>(param2));
		break;

	case imGetPreferredFrameSize:
		result = SDKPreferredFrameSize(stdParms, reinterpret_cast<imPreferredFrameSizeRec*>(param1));
		break;

	case imGetTimeInfo8: 
		result = SDKGetTimeInfo8(stdParms, reinterpret_cast<imFileRef>(param1), reinterpret_cast<imTimeInfoRec8*>(param2));
		break;

	case imGetMetaData:
		result = SDKGetMetaData(stdParms, reinterpret_cast<imFileRef>(param1), reinterpret_cast<imMetaDataRec*>(param2));
		break;

	case imSetMetaData:
		result = SDKSetMetaData(stdParms, reinterpret_cast<imFileRef>(param1), reinterpret_cast<imMetaDataRec*>(param2));
		break;

	case imGetSourceVideo:
		result = SDKGetSourceVideo(stdParms, reinterpret_cast<imFileRef>(param1), reinterpret_cast<imSourceVideoRec*>(param2));
		break;
	}
		
	return result;
}

static prMALError SDKInit(imStdParms *stdParms, imImportInfoRec *importInfo)
{
	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);
	av_log_set_callback(avlog_cb);

	importInfo->setupOnDblClk = kPrFalse;		// If user dbl-clicks file you imported, pop your setup dialog
	importInfo->canSave = kPrTrue;		// Can 'save as' files to disk, real file only

										// imDeleteFile8 is broken on MacOS when renaming a file using the Save Captured Files dialog
										// So it is not recommended to set this on MacOS yet (bug 1627325)
	importInfo->canDelete = kPrTrue;		// File importers only, use if you only if you have child files

	importInfo->dontCache = kPrFalse;		// Don't let Premiere cache these files
	importInfo->hasSetup = kPrTrue;		// Set to kPrTrue if you have a setup dialog
	importInfo->keepLoaded = kPrFalse;		// If you MUST stay loaded use, otherwise don't: play nice
	importInfo->priority = 0;
	importInfo->canTrim = kPrTrue;
	importInfo->canCalcSizes = kPrTrue;
	if (stdParms->imInterfaceVer >= IMPORTMOD_VERSION_6)
	{
		importInfo->avoidAudioConform = kPrTrue;
	}

	return malNoError;
}

static prMALError SDKGetIndFormat(imStdParms *stdParms, csSDK_size_t index,	imIndFormatRec *SDKIndFormatRec)
{
	prMALError	result = malNoError;

	switch (index)
	{
	case 0:
		SDKIndFormatRec->filetype = 'MKV_';
		SDKIndFormatRec->canWriteTimecode = kPrFalse;
		strcpy_s(SDKIndFormatRec->FormatName, sizeof(SDKIndFormatRec->FormatName), "Matroska");
		strcpy_s(SDKIndFormatRec->FormatShortName, sizeof(SDKIndFormatRec->FormatShortName), "Matroska");
		strcpy_s(SDKIndFormatRec->PlatformExtension, sizeof(SDKIndFormatRec->PlatformExtension), "mkv\0\0");
		break;
			
	default:
		result = imBadFormatIndex;
	}
	return result;
}

static prMALError SDKGetPrefs8(imStdParms *stdParms, imFileAccessRec8 *fileInfo8, imGetPrefsRec *prefsRec)
{
	impCtx *ldata;

	if (prefsRec->prefsLength == 0)
	{
		prefsRec->prefsLength = sizeof(impCtx);
	}
	else
	{
		ldata = (impCtx*)prefsRec->prefs;
		prUTF16CharCopy(ldata->fileName, fileInfo8->filepath);
	}

	return malNoError;
}

prMALError SDKOpenFile8(imStdParms *stdParms, imFileRef *SDKfileRef, imFileOpenRec8 *SDKfileOpenRec8)
{
	prMALError			result = malNoError;
	ImporterLocalRec8H	localRecH;

	DWORD				shareMode;

	if (SDKfileOpenRec8->privatedata)
	{
		localRecH = (ImporterLocalRec8H)SDKfileOpenRec8->privatedata;
	}
	else
	{
		localRecH = (ImporterLocalRec8H)stdParms->piSuites->memFuncs->newHandle(sizeof(impCtx));
		(*localRecH)->decoder = new Decoder();
		SDKfileOpenRec8->privatedata = (void*)localRecH;

	}

	// Open file according to access mode specified in imFileOpenRec.inReadWrite
	// Sometimes we will be asked to open with write access, for example to write
	// timecode metadata after a capture
	if (SDKfileOpenRec8->inReadWrite == kPrOpenFileAccess_ReadOnly)
	{
		shareMode = GENERIC_READ;
	}
	else if (SDKfileOpenRec8->inReadWrite == kPrOpenFileAccess_ReadWrite)
	{
		shareMode = GENERIC_WRITE;
	}

	size_t i;
	char *filename = (char*)malloc(kPrMaxPath);
	wcstombs_s(&i, filename, (size_t)kPrMaxPath, SDKfileOpenRec8->fileinfo.filepath, (size_t)kPrMaxPath);

	/* open input file, and allocate format context */
	if ((*localRecH)->decoder->open(filename) < 0)
	{
		result = GetLastError();

		// Make sure the file is closed if returning imBadFile.
		// Otherwise, a lower priority importer will not be able to open it.
		result = imBadFile;
	}
	else
	{
		*SDKfileRef = (*localRecH)->decoder;
		SDKfileOpenRec8->fileinfo.fileref = (*localRecH)->decoder;
		SDKfileOpenRec8->fileinfo.filetype = 'MKV_';
	}

	return result;
}

static prMALError SDKQuietFile(imStdParms *stdParms, imFileRef *SDKfileRef, void *privateData)
{
	Decoder *decoder = reinterpret_cast<Decoder*>(*SDKfileRef);

	// If file has not yet been closed
	if (decoder->isOpen)
	{
		decoder->close();
	}

	return malNoError;
}

static prMALError SDKCloseFile(imStdParms *stdParms, imFileRef *SDKfileRef, void *privateData)
{
	Decoder *decoder = reinterpret_cast<Decoder*>(*SDKfileRef);

	// If file has not yet been closed
	if (decoder->isOpen)
	{
		SDKQuietFile(stdParms, SDKfileRef, privateData);
	}

	// Remove the privateData handle.
	// CLEANUP - Destroy the handle we created to avoid memory leaks
	/*
	if (ldataH && *ldataH && (*ldataH)->BasicSuite)
	{
		(*ldataH)->BasicSuite->ReleaseSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion);
		(*ldataH)->BasicSuite->ReleaseSuite(kPrSDKPPixCacheSuite, kPrSDKPPixCacheSuiteVersion);
		(*ldataH)->BasicSuite->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
		(*ldataH)->BasicSuite->ReleaseSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion);
		stdParms->piSuites->memFuncs->disposeHandle(reinterpret_cast<char**>(ldataH));
	}
	*/

	return malNoError;
}

prMALError SDKGetInfo8(imStdParms *stdParms, imFileAccessRec8 *fileAccessInfo8, imFileInfoRec8 *SDKFileInfo8)
{
	prMALError result = malNoError;

	ImporterLocalRec8H ldataH = NULL;
	SDKFileInfo8->accessModes = kSeparateSequentialAudio;
	SDKFileInfo8->vidInfo.supportsAsyncIO = kPrFalse;
	SDKFileInfo8->vidInfo.supportsGetSourceVideo = kPrTrue;
	SDKFileInfo8->vidInfo.hasPulldown = kPrFalse;
	SDKFileInfo8->hasDataRate = kPrTrue;

	// Get a handle to our private data.  If it doesn't exist, allocate one
	// so we can use it to store our file instance info
	if (SDKFileInfo8->privatedata)
	{
		ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKFileInfo8->privatedata);
	}
	else
	{
		ldataH = reinterpret_cast<ImporterLocalRec8H>(stdParms->piSuites->memFuncs->newHandle(sizeof(impCtx)));
		SDKFileInfo8->privatedata = reinterpret_cast<void*>(ldataH);
	}
	
	impCtx *impCtx = *ldataH;
	Decoder *decoder = impCtx->decoder;

	// Either way, lock it in memory so it doesn't move while we modify it.
	stdParms->piSuites->memFuncs->lockHandle(reinterpret_cast<char**>(ldataH));

	// Find video stream
	AVStream *stream = decoder->findStream(AVMEDIA_TYPE_VIDEO);
	if (stream != NULL)
	{
		SDKFileInfo8->hasVideo = kPrTrue;
		SDKFileInfo8->vidInfo.subType = stream->codecpar->codec_tag;
		SDKFileInfo8->vidInfo.imageWidth = impCtx->width = stream->codecpar->width;
		SDKFileInfo8->vidInfo.imageHeight = impCtx->height = stream->codecpar->height;
		SDKFileInfo8->vidInfo.depth = stream->codecpar->bits_per_raw_sample;

		if (SDKFileInfo8->vidInfo.depth == 32)
		{
			SDKFileInfo8->vidInfo.alphaType = alphaStraight;
		}
		else
		{
			SDKFileInfo8->vidInfo.alphaType = alphaNone;
		}

		switch (stream->codecpar->field_order)
		{
		case AV_FIELD_BB:
		case AV_FIELD_BT:
			SDKFileInfo8->vidInfo.fieldType = prFieldsLowerFirst;
			break;

		case AV_FIELD_TB:
		case AV_FIELD_TT:
			SDKFileInfo8->vidInfo.fieldType = prFieldsUpperFirst;
			break;

		case AV_FIELD_PROGRESSIVE:
			SDKFileInfo8->vidInfo.fieldType = prFieldsNone;
			break;

		default:
			SDKFileInfo8->vidInfo.fieldType = prFieldsUnknown;
		}

		SDKFileInfo8->vidInfo.pixelAspectNum = stream->codecpar->sample_aspect_ratio.num;
		SDKFileInfo8->vidInfo.pixelAspectDen = stream->codecpar->sample_aspect_ratio.den;
		SDKFileInfo8->vidScale = impCtx->framerate.num = stream->r_frame_rate.num;
		SDKFileInfo8->vidSampleSize = impCtx->framerate.den = stream->r_frame_rate.den;

		SDKFileInfo8->vidDuration = 222;// decoder->getDuration();
	}
	else
	{
		SDKFileInfo8->hasVideo = kPrFalse;
		SDKFileInfo8->vidInfo.imageWidth = 0;
		SDKFileInfo8->vidInfo.imageHeight = 0;
	}

	// Find video stream
	/*
	stream = decoder->findStream(AVMEDIA_TYPE_AUDIO);
	if (stream != NULL)
	{
		SDKFileInfo8->hasAudio = kPrTrue;
	}
	else
	{
		SDKFileInfo8->hasAudio = kPrFalse;
	}
	*/



	/*

	// Initialize persistent storage
	(*ldataH)->audioPosition = 0;

	if ((**ldataH).theFile.hasAudio)
	{
		SDKFileInfo8->hasAudio = kPrTrue;

		// Importer API doesn't use channel-type enum from compiler API - need to map them
		if ((**ldataH).theFile.channelType == kPrAudioChannelType_Mono)
		{
			SDKFileInfo8->audInfo.numChannels = 1;
		}
		else if ((**ldataH).theFile.channelType == kPrAudioChannelType_Stereo)
		{
			SDKFileInfo8->audInfo.numChannels = 2;
		}
		else if ((**ldataH).theFile.channelType == kPrAudioChannelType_51)
		{
			SDKFileInfo8->audInfo.numChannels = 6;
		}
		else
		{
			returnValue = imBadFile;
		}

		SDKFileInfo8->audInfo.sampleRate = (float)(**ldataH).theFile.sampleRate;
		// 32 bit float only for now
		SDKFileInfo8->audInfo.sampleType = kPrAudioSampleType_32BitFloat;
		SDKFileInfo8->audDuration = (**ldataH).theFile.numSampleFrames;

#ifdef MULTISTREAM_AUDIO_TESTING
		if (!returnValue)
		{
			returnValue = MultiStreamAudioTesting(ldataH, SDKFileInfo8);
		}
#endif
	}
	else
	{
		SDKFileInfo8->hasAudio = kPrFalse;
	}


	*/
	int ret;


	// Acquire needed suites
	impCtx->memFuncs = stdParms->piSuites->memFuncs;
	impCtx->BasicSuite = stdParms->piSuites->utilFuncs->getSPBasicSuite();
	if (impCtx->BasicSuite)
	{
		impCtx->BasicSuite->AcquireSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion, (const void**)&impCtx->PPixCreatorSuite);
		impCtx->BasicSuite->AcquireSuite(kPrSDKPPixCreator2Suite, kPrSDKPPixCreator2SuiteVersion, (const void**)&impCtx->PPixCreator2Suite);
		impCtx->BasicSuite->AcquireSuite(kPrSDKPPixCacheSuite, kPrSDKPPixCacheSuiteVersion, (const void**)&impCtx->PPixCacheSuite);
		impCtx->BasicSuite->AcquireSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion, (const void**)&impCtx->PPixSuite);
		impCtx->BasicSuite->AcquireSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion, (const void**)&impCtx->PPix2Suite);
		impCtx->BasicSuite->AcquireSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion, (const void**)&impCtx->TimeSuite);
	}
	(*ldataH)->importerID = SDKFileInfo8->vidInfo.importerID;

	SDKFileInfo8->hasAudio = kPrFalse;
	prUTF16CharCopy((*ldataH)->fileName, fileAccessInfo8->filepath);

	stdParms->piSuites->memFuncs->unlockHandle(reinterpret_cast<char**>(ldataH));

	return result;
}

static prMALError SDKGetIndPixelFormat(imStdParms *stdParms, csSDK_size_t idx, imIndPixelFormatRec *SDKIndPixelFormatRec)
{
	prMALError	result = malNoError;
	impCtx *impCtx = *reinterpret_cast<ImporterLocalRec8H>(SDKIndPixelFormatRec->privatedata);

	switch (idx)
	{
	case 0:
		SDKIndPixelFormatRec->outPixelFormat = PrPixelFormat_YUV_420_MPEG4_FRAME_PICTURE_PLANAR_8u_709;
		break;

	default:
		result = imBadFormatIndex;
		break;
	}
	return result;
}

static prMALError SDKPreferredFrameSize(imStdParms *stdparms, imPreferredFrameSizeRec *preferredFrameSizeRec)
{
	prMALError result = malNoError;
	impCtx *impCtx = *reinterpret_cast<ImporterLocalRec8H>(preferredFrameSizeRec->inPrivateData);
	
	switch (preferredFrameSizeRec->inIndex)
	{
	case 0:
		preferredFrameSizeRec->outWidth = impCtx->width;
		preferredFrameSizeRec->outHeight = impCtx->height;
		result = malNoError;
		break;

	default:
		result = imOtherErr;
	}

	return result;
}

static prMALError SDKGetTimeInfo8(imStdParms *stdParms,	imFileRef SDKfileRef, imTimeInfoRec8 *SDKtimeInfoRec8)
{
	ImporterLocalRec8H ldataH = reinterpret_cast<ImporterLocalRec8H>(SDKtimeInfoRec8->privatedata);

/*	strcpy_s((SDKtimeInfoRec8)->altreel, 40, (*ldataH)->theFile.altreel);
	strcpy_s((SDKtimeInfoRec8)->alttime, 18, (*ldataH)->theFile.alttime);
	strcpy_s((SDKtimeInfoRec8)->orgreel, 40, (*ldataH)->theFile.orgreel);
	strcpy_s((SDKtimeInfoRec8)->orgtime, 18, (*ldataH)->theFile.orgtime);
	strcpy_s((SDKtimeInfoRec8)->logcomment, 256, (*ldataH)->theFile.logcomment);
	*/

	return malNoError;
}

static prMALError SDKGetMetaData(imStdParms *stdParms, imFileRef SDKfileRef, imMetaDataRec *SDKMetaDataRec)
{
	return malNoError;
}

static prMALError SDKSetMetaData(imStdParms *stdParms, imFileRef SDKfileRef, imMetaDataRec *SDKMetaDataRec)
{
	return malNoError;
}

static prMALError SDKGetSourceVideo(imStdParms *stdparms, imFileRef fileRef, imSourceVideoRec *sourceVideoRec)
{
	prMALError result = malNoError;

	impCtx *impCtx = *reinterpret_cast<ImporterLocalRec8H>(sourceVideoRec->inPrivateData);
	Decoder *decoder = impCtx->decoder;

	// Calculate premiere framerate
	PrTime ticksPerSecond = 0;
	impCtx->TimeSuite->GetTicksPerSecond(&ticksPerSecond);
	csSDK_int32 prFramerate = (ticksPerSecond * impCtx->framerate.den) / impCtx->framerate.num;
	
	csSDK_int32 theFrame = static_cast<csSDK_int32>(sourceVideoRec->inFrameTime / prFramerate);

	// Get the frame from the encoder
	AVFrame *frame = decoder->getFrameByTimestamp(sourceVideoRec->currentStreamIdx, theFrame);
	if (frame == NULL)
	{
		return malUnknownError;
	}

	// Get parameters for ReadFrameToBuffer()
	imFrameFormat *frameFormat = &sourceVideoRec->inFrameFormats[0];
	prRect theRect;
	if (frameFormat->inFrameWidth == 0 && frameFormat->inFrameHeight == 0)
	{
		frameFormat->inFrameWidth = impCtx->width;
		frameFormat->inFrameHeight = impCtx->height;
	}


	// Check to see if frame is already in cache
	result = impCtx->PPixCacheSuite->GetFrameFromCache(
		impCtx->importerID,
		sourceVideoRec->currentStreamIdx,
		theFrame,
		1,
		sourceVideoRec->inFrameFormats,
		sourceVideoRec->outFrame,
		NULL,
		NULL);

	if (result != suiteError_NoError)
	{
		// Create the ppix
		impCtx->PPixCreator2Suite->CreatePPix(
			sourceVideoRec->outFrame,
			PrPPixBufferAccess_ReadWrite,
			PrPixelFormat_YUV_420_MPEG4_FRAME_PICTURE_PLANAR_8u_709,
			impCtx->width,
			impCtx->height,
			frame->interlaced_frame ? kPrTrue : kPrFalse,
			theFrame,
			frame->sample_aspect_ratio.num,
			frame->sample_aspect_ratio.den);

		char *buffer[3];
		csSDK_uint32 stride[3];

		// Get planar buffers
		impCtx->PPix2Suite->GetYUV420PlanarBuffers(
			*sourceVideoRec->outFrame,
			PrPPixBufferAccess_ReadWrite,
			&buffer[0],
			&stride[0],
			&buffer[1],
			&stride[1],
			&buffer[2],
			&stride[2]);

		buffer[0] = (char*)frame->data[0];
		buffer[1] = (char*)frame->data[1];
		buffer[2] = (char*)frame->data[2];
		stride[0] = frame->linesize[0];
		stride[1] = frame->linesize[1];
		stride[2] = frame->linesize[2];

		// Put the ppix to the cache
		impCtx->PPixCacheSuite->AddFrameToCache(impCtx->importerID,
			sourceVideoRec->currentStreamIdx,
			*sourceVideoRec->outFrame,
			theFrame,
			NULL,
			NULL);

		result = malNoError;
	}
	
	return result;
}