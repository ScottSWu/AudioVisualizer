#include <Windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>

#include <stdio.h>

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

HRESULT RecordAudioStream(FILE * fout)
{
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	UINT32 bufferFrameCount;
	UINT32 numFramesAvailable;
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	IAudioClient *pAudioClient = NULL;
	IAudioCaptureClient *pCaptureClient = NULL;
	WAVEFORMATEX *pwfx = NULL;
	UINT32 packetLength = 0;
	BOOL bDone = FALSE;
	BYTE *pData;
	DWORD flags;

	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr);

	hr = pEnumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &pDevice);
	EXIT_ON_ERROR(hr);

	hr = pDevice->Activate(
		IID_IAudioClient, CLSCTX_ALL,
		NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr);

	hr = pAudioClient->GetMixFormat(&pwfx);
	EXIT_ON_ERROR(hr);

	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		hnsRequestedDuration,
		0,
		pwfx,
		NULL);
	EXIT_ON_ERROR(hr);

	// Get the size of the allocated buffer.
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	EXIT_ON_ERROR(hr);

	hr = pAudioClient->GetService(
		IID_IAudioCaptureClient,
		(void**)&pCaptureClient);
	EXIT_ON_ERROR(hr);

	// Notify the audio sink which format to use.
	printf("cbSize:   %d\n", pwfx->cbSize);
	printf("channels: %d\n", pwfx->nChannels);
	printf("samples:  %d\n", pwfx->nSamplesPerSec);
	printf("bits:     %d\n", pwfx->wBitsPerSample);
	printf("block:    %d\n", pwfx->nBlockAlign);
	printf("format:   %08x\n", pwfx->wFormatTag);
	WAVEFORMATEXTENSIBLE * pwfex = (WAVEFORMATEXTENSIBLE*)pwfx;
	printf("subformat: %08x %04x %04x\n",
		pwfex->SubFormat.Data1, pwfex->SubFormat.Data2, pwfex->SubFormat.Data3);
	printf("adpcm:     %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_ADPCM.Data1, KSDATAFORMAT_SUBTYPE_ADPCM.Data2,
		KSDATAFORMAT_SUBTYPE_ADPCM.Data3);
	printf("alaw:      %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_ALAW.Data1, KSDATAFORMAT_SUBTYPE_ALAW.Data2,
		KSDATAFORMAT_SUBTYPE_ALAW.Data3);
	printf("drm:       %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_DRM.Data1, KSDATAFORMAT_SUBTYPE_DRM.Data2,
		KSDATAFORMAT_SUBTYPE_DRM.Data3);
	printf("float:     %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.Data1, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.Data2,
		KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.Data3);
	printf("mpeg:      %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_MPEG.Data1, KSDATAFORMAT_SUBTYPE_MPEG.Data2,
		KSDATAFORMAT_SUBTYPE_MPEG.Data3);
	printf("pcm:       %08x %04x %04x\n",
		KSDATAFORMAT_SUBTYPE_PCM.Data1, KSDATAFORMAT_SUBTYPE_PCM.Data2,
		KSDATAFORMAT_SUBTYPE_PCM.Data3);
	
	// Calculate the actual duration of the allocated buffer.
	hnsActualDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec);

	// Write Wave file headers
	char WavChunkID[] = "RIFF";
	char WavFormat[] = "WAVE";
	char WavSubchunk1ID[] = "fmt ";
	char WavSubchunk2ID[] = "data";

	short WavAudioFormat = pwfx->wFormatTag;

	int WavSampleRate = pwfx->nSamplesPerSec;
	short WavNumChannels = pwfx->nChannels;
	short WavBitsPerSample = pwfx->wBitsPerSample;
	short WavBlockAlign = pwfx->nBlockAlign;
	int WavByteRate = WavSampleRate * WavNumChannels * WavBitsPerSample / 8;

	short WavExtraSize = pwfx->cbSize;
	short WavValidBitsPerSample = pwfex->Samples.wValidBitsPerSample;
	int WavChannelMask = pwfex->dwChannelMask;
	long WavSubFormat1 = pwfex->SubFormat.Data1;
	short WavSubFormat2 = pwfex->SubFormat.Data2;
	short WavSubFormat3 = pwfex->SubFormat.Data3;
	unsigned char * WavSubFormat4 = pwfex->SubFormat.Data4;

	int WavSubchunk1Size = 18 + WavExtraSize;
	int WavSubchunk2Size = WavSampleRate * 2 * WavNumChannels * WavBitsPerSample / 8;
	int WavChunkSize = 36 + WavSubchunk2Size;

	// RIFF WAVE header
	fwrite(WavChunkID, 1, 4, fout);
	fwrite(&WavChunkSize, 4, 1, fout);
	fwrite(WavFormat, 1, 4, fout);

	// Subchunk 1 format
	fwrite(WavSubchunk1ID, 1, 4, fout);
	fwrite(&WavSubchunk1Size, 4, 1, fout);
	fwrite(&WavAudioFormat, 2, 1, fout);
	fwrite(&WavNumChannels, 2, 1, fout);
	fwrite(&WavSampleRate, 4, 1, fout);
	fwrite(&WavByteRate, 4, 1, fout);
	fwrite(&WavBlockAlign, 2, 1, fout);
	fwrite(&WavBitsPerSample, 2, 1, fout);

	// Extensible
	fwrite(&WavExtraSize, 2, 1, fout);
	fwrite(&WavValidBitsPerSample, 2, 1, fout);
	fwrite(&WavChannelMask, 4, 1, fout);
	fwrite(&WavSubFormat1, 4, 1, fout);
	fwrite(&WavSubFormat2, 2, 1, fout);
	fwrite(&WavSubFormat3, 2, 1, fout);
	fwrite(WavSubFormat4, 1, 8, fout);

	// Subchunk 2 data
	fwrite(WavSubchunk2ID, 1, 4, fout);
	fwrite(&WavSubchunk2Size, 4, 1, fout);

	hr = pAudioClient->Start();  // Start recording.
	EXIT_ON_ERROR(hr);

	// Each loop fills about half of the shared buffer.
	int recordedSamples = 0;
	while (bDone == FALSE && recordedSamples < 44100 * 2)
	{
		// Sleep for half the buffer duration.
		Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

		hr = pCaptureClient->GetNextPacketSize(&packetLength);
		EXIT_ON_ERROR(hr);

		while (packetLength != 0)
		{
			// Get the available data in the shared buffer.
			hr = pCaptureClient->GetBuffer(
				&pData,
				&numFramesAvailable,
				&flags, NULL, NULL);
			EXIT_ON_ERROR(hr);

			if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
			{
				pData = NULL;  // Tell CopyData to write silence.
			}

			// Write the available capture data to the wave
			if (pData != NULL) {
				/*
				if (recordedSamples > 20000 && recordedSamples < 20500) {
					for (int i = 0; i < numFramesAvailable; i++) {
						for (int j = 0; j < WavBlockAlign; j++) {
							printf("%02x ", pData[i*WavBlockAlign + j]);
						}
						printf("\n");
					}
				}
				*/
				fwrite(pData, 1, numFramesAvailable * WavBlockAlign, fout);
				recordedSamples += numFramesAvailable;
			}

			hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
			EXIT_ON_ERROR(hr);

			hr = pCaptureClient->GetNextPacketSize(&packetLength);
			EXIT_ON_ERROR(hr);
		}
	}

	hr = pAudioClient->Stop();  // Stop recording.
	EXIT_ON_ERROR(hr);

Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pCaptureClient);

	return hr;
}

int main() {
	CoInitialize(NULL);
	FILE * f;
	fopen_s(&f, "test.wav", "wb");
	RecordAudioStream(f);
	fclose(f);
	return 0;
}
