/*
	ALICE SOFT SYSTEM 3 for Win32

	[ AGS ]
*/

#ifndef _AGS_H_
#define _AGS_H_

#include <stdio.h>
#include "../common.h"
#include "nact.h"
#include <SDL_ttf.h>
#include <map>

// フェード処理
static const int fade_x[16] = {0, 2, 2, 0, 1, 3, 3, 1, 1, 3, 3, 1, 0, 2, 2, 0};
static const int fade_y[16] = {0, 2, 0, 2, 1, 3, 1, 3, 0, 2, 0, 2, 1, 3, 1, 3};

#define MAX_CG 10000

static inline uint32 SETPALETTE256(uint32 R, uint32 G, uint32 B)
{
	return ((R & 0xff) << 16) | ((G & 0xff) << 8) | (B & 0xff);
}

static inline uint32 SETPALETTE16(uint32 R, uint32 G, uint32 B)
{
	R = (255 * (R & 0x0f)) / 15;
	G = (255 * (G & 0x0f)) / 15;
	B = (255 * (B & 0x0f)) / 15;
	return SETPALETTE256(R, G, B);
}

inline uint32* surface_line(SDL_Surface* surface, int y)
{
	return (uint32*)((uint8*)surface->pixels + surface->pitch * y);
}

struct Config;

class AGS
{
protected:
	NACT* nact;
private:
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;

	// Surface
	SDL_Surface* hBmpScreen[3]; // 8bpp * 3 (表, 裏, メニュー)
	SDL_Surface* hBmpDest; // DIBSection 24bpp (最終出力先)

	// フォント
	SDL_RWops* rw_font[3];
	TTF_Font* hFont16[3];
	TTF_Font* hFont24[3];
	TTF_Font* hFont32[3];
	TTF_Font* hFont48[3];
	TTF_Font* hFont64[3];
	std::map<int, TTF_Font*> hFontCustom[3];

	SDL_RWops* rw_vwidth_font[3];
	TTF_Font* hVWidthFont16[3];
	TTF_Font* hVWidthFont24[3];
	TTF_Font* hVWidthFont32[3];
	TTF_Font* hVWidthFont48[3];
	TTF_Font* hVWidthFont64[3];
	std::map<int, TTF_Font*> hVWidthFontCustom[3];

	// カーソル
	SDL_Cursor* hCursor[10];

	// AGS
	void load_gm3(uint8* data, int page, int transparent);		// Intruder -桜屋敷の探索-
	void load_vsp2l(uint8* data, int page, int transparent);	// Little Vampire
	void load_gl3(uint8* data, int page, int transparent);
	void load_pms(uint8* data, int page, int transparent);
	void load_bmp(const char* file_name);				// あゆみちゃん物語 フルカラー実写版
	void load_vsp(uint8* data, int page, int transparent);

	void draw_char(int dest, int dest_x, int dest_y, uint16 code, TTF_Font* font, uint8 color);
	void draw_char_antialias(int dest, int dest_x, int dest_y, uint16 code, TTF_Font* font, uint8 color, uint8 cache[]);
	void draw_gaiji(int dest, int dest_x, int dest_y, uint16 code, int size, uint8 color);

	void draw_window(int sx, int sy, int ex, int ey, bool frame, uint8 frame_color, uint8 back_color);

	void draw_screen(int sx, int sy, int width, int heignt);
	void invalidate_screen(int sx, int sy, int width, int height);

	uint8 palR(uint8 col) { return screen_palette[col] >> 16 & 0xff; }
	uint8 palG(uint8 col) { return screen_palette[col] >> 8 & 0xff; }
	uint8 palB(uint8 col) { return screen_palette[col] & 0xff; }
	int nearest_color(int r, int g, int b);

	uint32* vram[3][480];	// 仮想VRAMへのポインタ

	uint32 program_palette[256];
	uint32 screen_palette[256];
	uint8 gaiji[188][32];

	bool fader;	// フェードの状態
	uint32 fader_screen[640 * 480];

public:
	AGS(NACT* parent, const Config& config);
	~AGS();

	void update_screen();

	void flush_screen(bool update);

	void load_cg(int page, int transparent);
	bool load_custom_font(int fontSize);

	void set_palette(int index, int r, int g, int b);
	uint8 get_pixel(int dest, int x, int y);
	void set_pixel(int dest, int x, int y, uint8 color);

	void fade_start();
	void fade_end();
	void fade_out(int depth, bool white);
	void fade_in(int depth);
	bool now_fade() { return fader; }

	void copy(int sx, int sy, int ex, int ey, int dx, int dy);
	void gcopy(int gsc, int gde, int glx, int gly, int gsw);
	void paint(int x, int y, uint8 color);
	void draw_box(int index);
	void draw_mesh(int sx, int sy, int width, int height);
	void box_fill(int dest, int sx, int sy, int ex, int ey, uint8 color);
	void box_line(int dest, int sx, int sy, int ex, int ey, uint8 color);

	void draw_text(const char* string, bool text_wait = false);
	uint16 convert_zenkaku(uint16 code);
	uint16 convert_hankaku(uint16 code);

	void clear_text_window(int index, bool erase);
	bool return_text_line(int index);
	void draw_push(int index);
	void open_text_window(int index, bool erase);
	void close_text_window(int index, bool update);

	void clear_menu_window();
	void open_menu_window(int index);
	void redraw_menu_window(int index, int selected);
	void close_menu_window(int index);

	void load_cursor(int page);
	void select_cursor();

	void save_screenshot(const char* path);

	bool dirty;

	void set_menu_font_maxsize();
	void set_menu_monospace(int param);
	void set_text_monospace(int param);
	bool has_vwidth_font(int param);

	int cur_menu_monospace_font;
	int cur_text_monospace_font;
	int cur_menu_vwidth_font;
	int cur_text_vwidth_font;

	// ACG.DAT
	char acg[16];

	// 画面選択
	int src_screen;
	int dest_screen;

	int scroll;
	int screen_height;

	// メッセージ表示
	int text_dest_x;
	int text_dest_y;
	int text_space;
	int text_font_size;
	uint8 text_font_color;
	uint8 text_frame_color;
	uint8 text_back_color;
	int text_font_maxsize;	// その行での最大フォントサイズ

	// メニュー表示
	int menu_dest_x;
	int menu_dest_y;
	int menu_font_size;
	int menu_font_maxsize;
	uint8 menu_font_color;
	uint8 menu_frame_color;
	uint8 menu_back_color;
	bool menu_fix;

	int menu_max_height;
	int menu_max;

	bool draw_hankaku;
	bool draw_menu;
	bool draw_menu_monospace;
	bool draw_text_monospace;

	// ウィンドウ (Bコマンド)
	typedef struct {
		int sx;
		int sy;
		int ex;
		int ey;
		bool frame;
		bool push;
		uint32* screen;
		int screen_x;
		int screen_y;
		int screen_width;
		int screen_height;
		uint32 screen_palette[256];
		uint32* window;
		int window_x;
		int window_y;
		int window_width;
		int window_height;
		uint32 window_palette[256];
	} WINDOW;
	WINDOW menu_w[10];
	WINDOW text_w[10];

	// ボックス (E, Y7コマンド)
	typedef struct {
		uint8 color;
		int sx;
		int sy;
		int ex;
		int ey;
	} BOX;
	BOX box[20];

	// CG表示
	bool set_cg_dest;
	int cg_dest_x;
	int cg_dest_y;
	bool get_palette;
	bool extract_palette;
	bool extract_cg;
	bool extract_palette_cg[MAX_CG];
	int palette_bank;

	// マウスカーソル
	uint8 cursor_color;
	int cursor_index;
};

extern "C" void ags_setAntialiasedStringMode(int on);

#endif
