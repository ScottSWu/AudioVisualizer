#include <WinSock2.h>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>

#include <stdio.h>
#include <complex>
#include <valarray>

#include "easywsclient.hpp"

const double PI = 3.141592653589793238460;

void fft(std::valarray<std::complex<double>>& x) {
	const size_t N = x.size();
	if (N <= 1) return;

	// divide
	std::valarray<std::complex<double>> even = x[std::slice(0, N / 2, 2)];
	std::valarray<std::complex<double>>  odd = x[std::slice(1, N / 2, 2)];

	// conquer
	fft(even);
	fft(odd);

	// combine
	for (size_t k = 0; k < N / 2; ++k)
	{
		std::complex<double> t = std::polar(1.0, -2 * PI * k / N) * odd[k];
		x[k] = even[k] + t;
		x[k + N / 2] = even[k] - t;
	}
}

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

using easywsclient::WebSocket;

#define FFTSIZE 2048

HRESULT RecordAudioStream(WebSocket::pointer ws)
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

	std::valarray<std::complex<double>> signal(std::complex<double>(0.0, 0.0), FFTSIZE);

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
	int threshold = pwfx->nSamplesPerSec / 30;
	
	// Calculate the actual duration of the allocated buffer.
	hnsActualDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec);

	hr = pAudioClient->Start();  // Start recording.
	EXIT_ON_ERROR(hr);

	// Each loop fills about half of the shared buffer.
	int capturedFrames = 0;
	while (bDone == FALSE) {
		// Sleep for a bit
		Sleep(10);

		hr = pCaptureClient->GetNextPacketSize(&packetLength);
		EXIT_ON_ERROR(hr);

		while (packetLength != 0) {
			// Get the available data in the shared buffer.
			hr = pCaptureClient->GetBuffer(
				&pData,
				&numFramesAvailable,
				&flags, NULL, NULL);
			EXIT_ON_ERROR(hr);

			if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
				pData = NULL;  // Tell CopyData to write silence.
			}

			// Write the available capture data to the wave
			if (pData != NULL) {
				// Combine channels and convert data to complex float
				signal = signal.shift(numFramesAvailable);
				int channels = pwfx->nChannels;
				int bps = pwfx->wBitsPerSample / 8;
				for (unsigned int i = 0; i < numFramesAvailable; i++) {
					float sum = 0.f;
					for (int j = 0; j < pwfx->nChannels; j++) {
						int offset = i * channels + j;
						sum += ((float*)pData)[offset];
					}
					signal[signal.size() - numFramesAvailable - 1 + i] = std::complex<double>(sum);
				}

				// Only process and send data every few milliseconds
				capturedFrames += numFramesAvailable;
				if (ws && capturedFrames > threshold) {
					// Reset captured frames
					capturedFrames = 0;

					// Compute fft
					std::valarray<std::complex<double>> freqs = signal;
					fft(freqs);

					// Convert to byte array
					std::vector<unsigned char> data(freqs.size());
					for (int i = 0; i < freqs.size(); i++) {
						double mag = log10(sqrt(freqs[i].real() * freqs[i].real() + freqs[i].imag() * freqs[i].imag()));
						// Map 0 to 4 to 0 to 255
						int normalized = (int)((mag + 1) * 255 / 4);
						if (normalized > 255) normalized = 255;
						if (normalized < 0) normalized = 0;
						data[i] = (unsigned char)normalized;
					}

					// Send over websockets
					ws->sendBinary(data);
					ws->poll();
				}
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
	// Initialize winsock
	WSADATA data;
	WSAStartup(MAKEWORD(2, 2), &data);
	// Initialize wasapi
	CoInitialize(NULL);
	// Initialize websocket
	WebSocket::pointer ws = WebSocket::from_direct("localhost", 9000, "");
	RecordAudioStream(ws);
	return 0;
}
