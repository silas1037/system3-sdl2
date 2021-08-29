/*
	ALICE SOFT SYSTEM 3 for Win32

	[ MAKO ]
*/

#include <windows.h>
#undef ERROR
#include <memory>
#include <string>
#include <SDL.h>
#include <SDL_syswm.h>
#include "../sys/mako.h"
#include "../sys/mako_midi.h"
#include "../fm/mako_fmgen.h"
#include "../config.h"
#include "../sys/dri.h"
#include "../sys/crc32.h"

extern SDL_Window* g_window;

namespace {

const int SAMPLE_RATE = 44100;

// Per-game mapping from music numbers to CD tracks.  This is necessary to
// forcibly change the sound device with a menu command.
//
// When the game uses "Z 100+x,y" command, the xth element of the array should
// be y.  The array must be terminated with -1.
const int8 RANCE41_tracks[] = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,1,-1};
const int8 RANCE42_tracks[] = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,1,-1};
const int8 DPSALL_tracks[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,5,1,2,3,-1};

enum MakoThreadMessage {
	MAKO_OPEN = WM_APP,
	MAKO_PLAY,
	MAKO_STOP,
	MAKO_EXIT
};

enum MakoOpenFlags {
	OPEN_CD = 0,
	OPEN_MIDI = 1,
};

const char* mci_geterror(DWORD err)
{
	static char buf[128];
	mciGetErrorString(err, buf, sizeof(buf));
	return buf;
}

class MCIThread {
public:
	MCIThread(NACT* nact, HWND hwnd_notify) : hwnd_notify(hwnd_notify) {
		if (!_beginthreadex(NULL, 0, &MCIThread::run, this, 0, &thread_id))
			nact->fatal("Cannot create thread: %s", strerror(errno));
	}

	bool post_message(UINT msg, WPARAM wparam, LPARAM lparam) {
		return PostThreadMessage(thread_id, msg, wparam, lparam);
	}

	MCIDEVICEID device_id() { return devid; }

private:
	static unsigned __stdcall run(void* param)
	{
		MCIThread* t = reinterpret_cast<MCIThread*>(param);
		t->message_loop();
		delete t;
		return 0;
	}

	void message_loop()
	{
		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0)) {
			switch (msg.message) {
			case MAKO_OPEN:
				close();
				open(reinterpret_cast<std::string*>(msg.wParam),
					 static_cast<MakoOpenFlags>(msg.lParam));
				break;

			case MAKO_PLAY:
				play();
				break;

			case MAKO_STOP:
				close();
				break;

			case MAKO_EXIT:
				close();
				return;
			}
		}
	}

	void open(std::string* file, MakoOpenFlags open_flags)
	{
		file_playing = file;
		is_midi = open_flags & OPEN_MIDI;

		MCI_OPEN_PARMS open;
		open.lpstrElementName = file_playing->c_str();
		DWORD flags = MCI_OPEN_ELEMENT | MCI_WAIT;
		if (is_midi) {
			flags |= MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID;
			open.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_SEQUENCER;
		}
		MCIERROR err = mciSendCommand(0, MCI_OPEN, flags, (DWORD_PTR)&open);
		if (err) {
			WARNING("MCI_OPEN failed: %s: %s",
					file_playing->c_str(), mci_geterror(err));
			return;
		}
		devid = open.wDeviceID;
	}

	void play()
	{
		MCI_PLAY_PARMS play;
		play.dwCallback = (DWORD_PTR)hwnd_notify;
		play.dwFrom = 0;
		DWORD flags = MCI_FROM | MCI_NOTIFY;
		MCIERROR err = mciSendCommand(devid, MCI_PLAY, flags, (DWORD_PTR)&play);
		if (err)
			WARNING("MCI_PLAY failed: %s", mci_geterror(err));
	}

	void close() {
		if (devid) {
			mciSendCommand(devid, MCI_CLOSE, 0, 0);
			devid = 0;
		}
		if (file_playing) {
			if (is_midi)
				DeleteFile(file_playing->c_str());  // remove temporary file
			delete file_playing;
			file_playing = nullptr;
		}
	}

	HWND hwnd_notify;
	unsigned thread_id;
	std::string* file_playing = nullptr;
	bool is_midi;
	MCIDEVICEID devid = 0;
};

MCIThread* mci_thread;

class Music {
public:
	Music(const char* fname, int loop) : loops(loop) {
		std::string* fname_str = new std::string(fname);
		mci_thread->post_message(MAKO_OPEN, (WPARAM)fname_str, OPEN_CD);
		mci_thread->post_message(MAKO_PLAY, 0, 0);
		playing = true;
	}

	Music(std::unique_ptr<MAKOMidi> midi, int loop) : loops(loop ? 1 : 0) {
		std::vector<uint8> smf = midi->generate_smf(loop);
		char path[MAX_PATH + 1];
		if (!GetTempPath(sizeof(path), path)) {
			WARNING("GetTempPath failed: 0x%x", GetLastError());
			return;
		}
		char fname[MAX_PATH + 1];
		if (!GetTempFileName(path, "mid", 0, fname)) {
			WARNING("GetTempFileName failed: 0x%x", GetLastError());
			return;
		}
		FILE* fp = fopen(fname, "wb");
		if (!fp) {
			WARNING("Cannot open %s: %s", fname, strerror(errno));
			return;
		}
		if (fwrite(smf.data(), smf.size(), 1, fp) != 1) {
			WARNING("Cannot write to %s: %s", fname, strerror(errno));
			fclose(fp);
			DeleteFile(fname);
			return;
		}
		fclose(fp);

		std::string* fname_str = new std::string(fname);
		mci_thread->post_message(MAKO_OPEN, (WPARAM)fname_str, OPEN_MIDI);
		mci_thread->post_message(MAKO_PLAY, 0, 0);
		playing = true;
	}

	~Music() {
		close();
	}

	void close() {
		mci_thread->post_message(MAKO_STOP, 0, 0);
		playing = false;
	}

	bool is_playing() {
		return playing;
	}

	bool on_mci_notify(const SDL_SysWMmsg* msg) {
		if (msg->msg.win.lParam != mci_thread->device_id())
			return true;
		switch (msg->msg.win.wParam) {
		case MCI_NOTIFY_FAILURE:
			close();
			return false;
		case MCI_NOTIFY_SUCCESSFUL:
			if (loops && --loops == 0) {
				close();
				return false;
			}
			mci_thread->post_message(MAKO_PLAY, 0, 0);
			return true;
		}
		return true;
	}

private:
	int loops;  // number of times to play, 0 for infinite loop
	bool playing;
};

Music* music;
uint8* wav_buffer;
SDL_mutex* fm_mutex;
std::unique_ptr<MakoFMgen> fm;

void audio_callback(void*, Uint8* stream, int len) {
	SDL_LockMutex(fm_mutex);
	fm->Process(reinterpret_cast<int16*>(stream), len/ 4);
	SDL_UnlockMutex(fm_mutex);
}

} // namespace

MAKO::MAKO(NACT* parent, const Config& config) :
	use_fm(config.use_fm),
	current_music(0),
	next_loop(0),
	nact(parent)
{
	if (!config.playlist.empty())
		load_playlist(config.playlist.c_str());

	strcpy_s(amus, 16, "AMUS.DAT");
	strcpy_s(amse, 16, "AMSE.DAT");	// 実際には使わない

	for(int i = 1; i <= 99; i++) {
		cd_track[i] = 0;
	}

	SDL_InitSubSystem(SDL_INIT_AUDIO);
	SDL_AudioSpec fmt;
	SDL_zero(fmt);
	fmt.freq = SAMPLE_RATE;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 4096;
	fmt.callback = &audio_callback;
	if (SDL_OpenAudio(&fmt, NULL) < 0) {
		WARNING("SDL_OpenAudio: %s", SDL_GetError());
		use_fm = false;
	}
	fm_mutex = SDL_CreateMutex();

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(g_window, &info))
		parent->fatal("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
	mci_thread = new MCIThread(parent, info.info.win.window);
}

MAKO::~MAKO()
{
	stop_music();
	stop_pcm();
	mci_thread->post_message(MAKO_EXIT, 0, 0);
	mci_thread = nullptr;  // MCIThread self-destructs on the worker thread.

	SDL_LockMutex(fm_mutex);
	SDL_CloseAudio();
	SDL_UnlockMutex(fm_mutex);
	SDL_DestroyMutex(fm_mutex);
	fm_mutex = nullptr;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool MAKO::load_playlist(const char* path)
{
	FILE* fp = fopen(path, "rt");
	if (!fp) {
		WARNING("Cannot open %s", path);
		return false;
	}
	char buf[256];
	while (fgets(buf, sizeof(buf) - 1, fp)) {
		char *p = &buf[strlen(buf) - 1];
		if (*p == '\n')
			*p = '\0';
		playlist.push_back(buf[0] ? strdup(buf) : NULL);
	}
	fclose(fp);
	return true;
}

void MAKO::play_music(int page)
{
	if (current_music == page)
		return;

	stop_music();

	size_t track = page < 100 ? cd_track[page] : 0;
	if (track) {
		if (track >= playlist.size() || !playlist[track])
			return;
		music = new Music(playlist[track], next_loop);
	} else if (use_fm) {
		DRI dri;
		int size;
		uint8* data = dri.load(amus, page, &size);
		if (!data)
			return;

		SDL_LockMutex(fm_mutex);
		fm = std::make_unique<MakoFMgen>(SAMPLE_RATE, data, true);
		SDL_UnlockMutex(fm_mutex);
		SDL_PauseAudio(0);
	} else {
		auto midi = std::make_unique<MAKOMidi>(nact, amus);
		if (!midi->load_mml(page))
			return;
		midi->load_mda(page);
		music = new Music(std::move(midi), next_loop);
	}
	current_music = page;
}

void MAKO::stop_music()
{
	if (music) {
		delete music;
		music = nullptr;
	}
	SDL_PauseAudio(1);
	current_music = 0;
}

bool MAKO::check_music()
{
	return music && music->is_playing();
}

void MAKO::select_sound(BGMDevice dev)
{
	// 強制的に音源を変更する
	int page = current_music;
	int old_dev = (1 <= page && page <= 99 && cd_track[page]) ? BGM_CD :
		use_fm ? BGM_FM : BGM_MIDI;

	switch (dev) {
	case BGM_FM:
	case BGM_MIDI:
		for (int i = 1; i <= 99; i++)
			cd_track[i] = 0;
		use_fm = dev == BGM_FM;
		break;

	case BGM_CD:
		const int8* tracks;

		switch (nact->crc32_a) {
		case CRC32_RANCE41:
		case CRC32_RANCE41_ENG:
			tracks = RANCE41_tracks;
			break;
		case CRC32_RANCE42:
		case CRC32_RANCE42_ENG:
			tracks = RANCE42_tracks;
			break;
		case CRC32_DPSALL:
			tracks = DPSALL_tracks;
			break;

		// For the following games, the default mapping (cd_track[i] = i) works.
		case CRC32_AYUMI_CD:
		case CRC32_FUNNYBEE_CD:
		case CRC32_ONLYYOU:
		default:
			tracks = nullptr;
		}

		if (tracks) {
			for (int i = 0; tracks[i] >= 0; i++)
				cd_track[i + 1] = tracks[i];
		} else {
			for (int i = 1; i <= 99; i++)
				cd_track[i] = i;
		}
		break;
	}

	// デバイスが変更された場合は再演奏する
	if (dev != old_dev && page) {
		stop_music();
		play_music(page);
	}
}

void MAKO::get_mark(int* mark, int* loop)
{
	// TODO: fix
	*mark = 0;
	*loop = 0;
}

void MAKO::play_pcm(int page, bool loop)
{
	static char header[44] = {
		'R' , 'I' , 'F' , 'F' , 0x00, 0x00, 0x00, 0x00, 'W' , 'A' , 'V' , 'E' , 'f' , 'm' , 't' , ' ' ,
		0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x40, 0x1f, 0x00, 0x00,
		0x01, 0x00, 0x08, 0x00, 'd' , 'a' , 't' , 'a' , 0x00, 0x00, 0x00, 0x00
	};

	stop_pcm();

	uint8* buffer = NULL;
	int size;
	DRI* dri = new DRI();

	if ((buffer = dri->load("AWAV.DAT", page, &size)) != NULL) {
		// WAV形式 (Only You)
		wav_buffer = buffer;
	} else if ((buffer = dri->load("AMSE.DAT", page, &size)) != NULL) {
		// AMSE形式 (乙女戦記)
		int total = (size - 12) * 2 + 0x24;
		int samples = (size - 12) * 2;

		wav_buffer = (uint8*)malloc(total + 8);
		memcpy(wav_buffer, header, 44);
		wav_buffer[ 4] = (total >>  0) & 0xff;
		wav_buffer[ 5] = (total >>  8) & 0xff;
		wav_buffer[ 6] = (total >> 16) & 0xff;
		wav_buffer[ 7] = (total >> 24) & 0xff;
		wav_buffer[40] = (samples >>  0) & 0xff;
		wav_buffer[41] = (samples >>  8) & 0xff;
		wav_buffer[42] = (samples >> 16) & 0xff;
		wav_buffer[43] = (samples >> 24) & 0xff;
		for(int i = 12, p = 44; i < size; i++) {
			wav_buffer[p++] = buffer[i] & 0xf0;
			wav_buffer[p++] = (buffer[i] & 0x0f) << 4;
		}
		free(buffer);
	}
	PlaySound((LPCTSTR)wav_buffer, NULL, SND_ASYNC | SND_MEMORY | (loop ? SND_LOOP : 0));
}

void MAKO::stop_pcm()
{
	PlaySound(NULL, NULL, SND_PURGE);
	if (wav_buffer) {
		free(wav_buffer);
		wav_buffer = nullptr;
	}
}

bool MAKO::check_pcm()
{
	// 再生中でtrue
	static const char null_wav[45] =  {
		'R' , 'I' , 'F' , 'F' , 0x25, 0x00, 0x00, 0x00, 'W' , 'A' , 'V' , 'E' , 'f' , 'm' , 't' , ' ' ,
		0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x40, 0x1f, 0x00, 0x00,
		0x01, 0x00, 0x08, 0x00, 'd' , 'a' , 't' , 'a' , 0x01, 0x00, 0x00, 0x00, 0x00
	};
	return !PlaySound(null_wav, NULL, SND_ASYNC | SND_MEMORY | SND_NOSTOP);
}

void MAKO::on_mci_notify(const SDL_SysWMmsg* msg)
{
	if (!music)
		return;
	if (!music->on_mci_notify(msg))
		current_music = 0;
}
