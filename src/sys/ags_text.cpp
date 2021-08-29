/*
	ALICE SOFT SYSTEM 3 for Win32

	[ AGS - text ]
*/

#include <string.h>
#include "ags.h"
#include "utfsjis.h"
#include "texthook.h"

static bool antialias = true;

void AGS::draw_text(const char* string, bool text_wait)
{
	int p = 0;
	int screen, dest_x, dest_y, font_size;
	uint8 font_color;

	uint8 antialias_cache[256*7];
	if (antialias)
		memset(antialias_cache, 0, 256);

	if (draw_menu) {
		screen = 2;
		dest_x = menu_dest_x;
		dest_y = menu_dest_y;
		font_size = menu_font_size;
		font_color = menu_font_color;
	} else {
		screen = dest_screen;
		dest_x = text_dest_x;
		dest_y = text_dest_y;
		font_size = text_font_size;
		font_color = text_font_color;
		if (*string && text_font_maxsize < text_font_size)
			text_font_maxsize = text_font_size;
	}

	TTF_Font* font = NULL;
	switch (font_size) {
	case 16: font = hFont16; break;
	case 24: font = hFont24; break;
	case 32: font = hFont32; break;
	case 48: font = hFont48; break;
	case 64: font = hFont64; break;
	}
	int ascent = TTF_FontAscent(font);
	int descent = TTF_FontDescent(font);
	// Adjust dest_y if the font height is larger than the specified size.
	dest_y -= (ascent - descent - font_size) / 2;

	while(string[p] != '\0') {
		// 文字コード取得
		uint8 c = (uint8)string[p++];
		uint16 code = c;
		if(is_2byte_message(c)) {
			code = (code << 8) | (uint8)string[p++];
		}
		if(draw_hankaku) {
			code = convert_hankaku(code);
		} else {
			code = convert_zenkaku(code);
		}

		// 文字出力
		if((0xff40 <= code && code <= 0xff9d) || (0xff9e <= code && code <= 0xfffc)) {
			// Use unadjusted dest_y here.
			draw_gaiji(screen, dest_x, draw_menu ? menu_dest_y : text_dest_y, code, font_size, font_color);
			dest_x += font_size;
		} else {
			int unicode = sjis_to_unicode(code);
			if (!draw_menu)
				texthook_character(nact->get_scenario_page(), unicode);

			if (antialias)
				draw_char_antialias(screen, dest_x, dest_y, unicode, font, font_color, antialias_cache);
			else
				draw_char(screen, dest_x, dest_y, unicode, font, font_color);

			int miny, maxy, advance;
			TTF_GlyphMetrics(font, code, NULL, NULL, &miny, &maxy, &advance);
			// Some fonts report incorrect Ascent/Descent value so we need to fix them.
			if (miny < descent) descent = miny;
			if (maxy > ascent) ascent = maxy;
			dest_x += advance;
		}

		if (!draw_menu && text_wait && c != ' ') {
			// 画面更新
			if(screen == 0)
				draw_screen(text_dest_x, dest_y, dest_x - text_dest_x, ascent - descent);
			text_dest_x = dest_x;
			// Wait
			nact->text_wait();
		}
	}
	if (draw_menu)
		menu_dest_x = dest_x;
	else if (!text_wait) {
		// 画面更新
		if(screen == 0)
			draw_screen(text_dest_x, dest_y, dest_x - text_dest_x, ascent - descent);
		text_dest_x = dest_x;
	}
}

void AGS::draw_char(int dest, int dest_x, int dest_y, uint16 code, TTF_Font* font, uint8 color)
{
	// パターン取得
	SDL_Color white = {0xff, 0xff, 0xff};
	SDL_Surface* fs = TTF_RenderGlyph_Solid(font, code, white);

	// パターン出力
	for(int y = 0; y < fs->h && dest_y + y < 480; y++) {
		uint8 *pattern = (uint8*)surface_line(fs, y);	// FIXME: do not assume 8bpp
		for(int x = 0; x < fs->w && dest_x + x < 640; x++) {
			if(pattern[x] != 0) {
				vram[dest][dest_y + y][dest_x + x] = color;
			}
		}
	}

	SDL_FreeSurface(fs);
}

int AGS::nearest_color(int r, int g, int b) {
	int i, col, mind = INT_MAX;
	for (i = 0; i < 256; i++) {
		int dr = r - palR(i);
		int dg = g - palG(i);
		int db = b - palB(i);
		int d = dr*dr*30 + dg*dg*59 + db*db*11;
		if (d < mind) {
			mind = d;
			col = i;
		}
	}
	return col;
}

void AGS::draw_char_antialias(int dest, int dest_x, int dest_y, uint16 code, TTF_Font* font, uint8 color, uint8 cache[])
{
	// パターン取得
	SDL_Color black = {0, 0, 0};
	SDL_Color white = {0xff, 0xff, 0xff};
	SDL_Surface* fs = TTF_RenderGlyph_Shaded(font, code, white, black);

	// パターン出力
	for(int y = 0; y < fs->h && dest_y + y < 480; y++) {
		uint8 *pattern = (uint8*)surface_line(fs, y);
		uint32 *dp = &vram[dest][dest_y + y][dest_x];
		for(int x = 0; x < fs->w && dest_x + x < 640; x++, dp++) {
			uint8 bg = *dp;
			int alpha = pattern[x] >> 5;
			if (alpha == 0) {
				// Transparent, do nothing
			} else if (alpha == 7) {
				*dp = color;
			} else if (cache[bg] & 1 << alpha) {
				*dp = cache[alpha << 8 | bg];
			} else {
				cache[bg] |= 1 << alpha;
				int c = nearest_color((palR(color) * alpha + palR(bg) * (7 - alpha)) / 7,
									  (palG(color) * alpha + palG(bg) * (7 - alpha)) / 7,
									  (palB(color) * alpha + palB(bg) * (7 - alpha)) / 7);
				cache[alpha << 8 | bg] = c;
				*dp = c;
			}
		}
	}

	SDL_FreeSurface(fs);
}

void AGS::draw_gaiji(int dest, int dest_x, int dest_y, uint16 code, int size, uint8 color)
{
	int index = (0xff40 <= code && code <= 0xff9d) ? code - 0xff40 : (0xff9e <= code && code <= 0xfffc) ? code - 0xff9e + 94 : 0;
	bool pattern[16][16];

	// パターン取得
	for(int y = 0; y < 16; y++) {
		uint8 l = gaiji[index][y * 2 + 0];
		uint8 r = gaiji[index][y * 2 + 1];

		pattern[y][ 0] = ((l & 0x80) != 0);
		pattern[y][ 1] = ((l & 0x40) != 0);
		pattern[y][ 2] = ((l & 0x20) != 0);
		pattern[y][ 3] = ((l & 0x10) != 0);
		pattern[y][ 4] = ((l & 0x08) != 0);
		pattern[y][ 5] = ((l & 0x04) != 0);
		pattern[y][ 6] = ((l & 0x02) != 0);
		pattern[y][ 7] = ((l & 0x01) != 0);
		pattern[y][ 8] = ((r & 0x80) != 0);
		pattern[y][ 9] = ((r & 0x40) != 0);
		pattern[y][10] = ((r & 0x20) != 0);
		pattern[y][11] = ((r & 0x10) != 0);
		pattern[y][12] = ((r & 0x08) != 0);
		pattern[y][13] = ((r & 0x04) != 0);
		pattern[y][14] = ((r & 0x02) != 0);
		pattern[y][15] = ((r & 0x01) != 0);
	}

	// パターン出力
	for(int y = 0; y < size && dest_y + y < 480; y++) {
		for(int x = 0; x < size && dest_x + x < 640; x++) {
			if(pattern[(y * 16) / size][(x * 16) / size]) {
				vram[dest][dest_y + y][dest_x + x] = color;
			}
		}
	}
}

uint16 AGS::convert_zenkaku(uint16 code)
{
	switch(code) {
		case 0x20: return 0xa1a1; // '　'
		case 0x21: return 0xa3a1; // '！'
		case 0x22: return 0xa1b1; // '”'
		case 0x23: return 0xa3a3; // '＃'
		case 0x24: return 0xa1e7; // '＄'
		case 0x25: return 0xa3a5; // '％'
		case 0x26: return 0xa3a6; // '＆'
		case 0x27: return 0xa1af; // '’'
		case 0x28: return 0xa3a8; // '（'
		case 0x29: return 0xa3a9; // '）'
		case 0x2a: return 0xa3aa; // '＊'
		case 0x2b: return 0xa3ab; // '＋'
		case 0x2c: return 0xa3ac; // '，'
		case 0x2d: return 0xa3ad; // '－'
		case 0x2e: return 0xa3ae; // '．'
		case 0x2f: return 0xa3af; // '／'
		case 0x30: return 0xa3b0; // '０'
		case 0x31: return 0xa3b1; // '１'
		case 0x32: return 0xa3b2; // '２'
		case 0x33: return 0xa3b3; // '３'
		case 0x34: return 0xa3b4; // '４'
		case 0x35: return 0xa3b5; // '５'
		case 0x36: return 0xa3b6; // '６'
		case 0x37: return 0xa3b7; // '７'
		case 0x38: return 0xa3b8; // '８'
		case 0x39: return 0xa3b9; // '９'
		case 0x3a: return 0xa3ba; // '：'
		case 0x3b: return 0xa3bb; // '；'
		case 0x3c: return 0xa3bc; // '＜'
		case 0x3d: return 0xa3bd; // '＝'
		case 0x3e: return 0xa3be; // '＞'
		case 0x3f: return 0xa3bf; // '？'
		case 0x40: return 0xa3c0; // '＠'
		case 0x41: return 0xa3c1; // 'Ａ'
		case 0x42: return 0xa3c2; // 'Ｂ'
		case 0x43: return 0xa3c3; // 'Ｃ'
		case 0x44: return 0xa3c4; // 'Ｄ'
		case 0x45: return 0xa3c5; // 'Ｅ'
		case 0x46: return 0xa3c6; // 'Ｆ'
		case 0x47: return 0xa3c7; // 'Ｇ'
		case 0x48: return 0xa3c8; // 'Ｈ'
		case 0x49: return 0xa3c9; // 'Ｉ'
		case 0x4a: return 0xa3ca; // 'Ｊ'
		case 0x4b: return 0xa3cb; // 'Ｋ'
		case 0x4c: return 0xa3cc; // 'Ｌ'
		case 0x4d: return 0xa3cd; // 'Ｍ'
		case 0x4e: return 0xa3ce; // 'Ｎ'
		case 0x4f: return 0xa3cf; // 'Ｏ'
		case 0x50: return 0xa3d0; // 'Ｐ'
		case 0x51: return 0xa3d1; // 'Ｑ'
		case 0x52: return 0xa3d2; // 'Ｒ'
		case 0x53: return 0xa3d3; // 'Ｓ'
		case 0x54: return 0xa3d4; // 'Ｔ'
		case 0x55: return 0xa3d5; // 'Ｕ'
		case 0x56: return 0xa3d6; // 'Ｖ'
		case 0x57: return 0xa3d7; // 'Ｗ'
		case 0x58: return 0xa3d8; // 'Ｘ'
		case 0x59: return 0xa3d9; // 'Ｙ'
		case 0x5a: return 0xa3da; // 'Ｚ'
		case 0x5b: return 0xa3db; // '［'
		case 0x5c: return 0xa3a4; // '￥'
		case 0x5d: return 0xa3dd; // '］'
		case 0x5e: return 0xa3de; // '＾'
		case 0x5f: return 0xa3df; // '＿'
		case 0x60: return 0xa1ae; // '‘'
		case 0x61: return 0xa3e1; // 'ａ'
		case 0x62: return 0xa3e2; // 'ｂ'
		case 0x63: return 0xa3e3; // 'ｃ'
		case 0x64: return 0xa3e4; // 'ｄ'
		case 0x65: return 0xa3e5; // 'ｅ'
		case 0x66: return 0xa3e6; // 'ｆ'
		case 0x67: return 0xa3e7; // 'ｇ'
		case 0x68: return 0xa3e8; // 'ｈ'
		case 0x69: return 0xa3e9; // 'ｉ'
		case 0x6a: return 0xa3ea; // 'ｊ'
		case 0x6b: return 0xa3eb; // 'ｋ'
		case 0x6c: return 0xa3ec; // 'ｌ'
		case 0x6d: return 0xa3ed; // 'ｍ'
		case 0x6e: return 0xa3ee; // 'ｎ'
		case 0x6f: return 0xa3ef; // 'ｏ'
		case 0x70: return 0xa3f0; // 'ｐ'
		case 0x71: return 0xa3f1; // 'ｑ'
		case 0x72: return 0xa3f2; // 'ｒ'
		case 0x73: return 0xa3f3; // 'ｓ'
		case 0x74: return 0xa3f4; // 'ｔ'
		case 0x75: return 0xa3f5; // 'ｕ'
		case 0x76: return 0xa3f6; // 'ｖ'
		case 0x77: return 0xa3f7; // 'ｗ'
		case 0x78: return 0xa3f8; // 'ｘ'
		case 0x79: return 0xa3f9; // 'ｙ'
		case 0x7a: return 0xa3fa; // 'ｚ'
		case 0x7b: return 0xa3fb; // '｛'
		case 0x7c: return 0xa3fc; // '｜'
		case 0x7d: return 0xa3fd; // '｝'
		case 0x7e: return 0xa1ab; // '～'
		case 0xa1: return 0xa1a3; // '。'
		case 0xa2: return 0xa1b8; // '「'
		case 0xa3: return 0xa1b9; // '」'
		case 0xa4: return 0xa1a2; // '、'
		case 0xa5: return 0xa1a4; // '・'
		case 0xa6: return 0xa4f2; // 'を'
		case 0xa7: return 0xa4a1; // 'ぁ'
		case 0xa8: return 0xa4a3; // 'ぃ'
		case 0xa9: return 0xa4a5; // 'ぅ'
		case 0xaa: return 0xa4a7; // 'ぇ'
		case 0xab: return 0xa4a9; // 'ぉ'
		case 0xac: return 0xa4e3; // 'ゃ'
		case 0xad: return 0xa4e5; // 'ゅ'
		case 0xae: return 0xa4e7; // 'ょ'
		case 0xaf: return 0xa4c3; // 'っ'
		case 0xb0: return 0xa960; // 'ー'
		case 0xb1: return 0xa4a2; // 'あ'
		case 0xb2: return 0xa4a4; // 'い'
		case 0xb3: return 0xa4a6; // 'う'
		case 0xb4: return 0xa4a8; // 'え'
		case 0xb5: return 0xa4aa; // 'お'
		case 0xb6: return 0xa4ab; // 'か'
		case 0xb7: return 0xa4ad; // 'き'
		case 0xb8: return 0xa4af; // 'く'
		case 0xb9: return 0xa4b1; // 'け'
		case 0xba: return 0xa4b3; // 'こ'
		case 0xbb: return 0xa4b5; // 'さ'
		case 0xbc: return 0xa4b7; // 'し'
		case 0xbd: return 0xa4b9; // 'す'
		case 0xbe: return 0xa4bb; // 'せ'
		case 0xbf: return 0xa4bd; // 'そ'
		case 0xc0: return 0xa4bf; // 'た'
		case 0xc1: return 0xa4c1; // 'ち'
		case 0xc2: return 0xa4c4; // 'つ'
		case 0xc3: return 0xa4c6; // 'て'
		case 0xc4: return 0xa4c8; // 'と'
		case 0xc5: return 0xa4ca; // 'な'
		case 0xc6: return 0xa4cb; // 'に'
		case 0xc7: return 0xa4cc; // 'ぬ'
		case 0xc8: return 0xa4cd; // 'ね'
		case 0xc9: return 0xa4ce; // 'の'
		case 0xca: return 0xa4cf; // 'は'
		case 0xcb: return 0xa4d2; // 'ひ'
		case 0xcc: return 0xa4d5; // 'ふ'
		case 0xcd: return 0xa4d8; // 'へ'
		case 0xce: return 0xa4db; // 'ほ'
		case 0xcf: return 0xa4de; // 'ま'
		case 0xd0: return 0xa4df; // 'み'
		case 0xd1: return 0xa4e0; // 'む'
		case 0xd2: return 0xa4e1; // 'め'
		case 0xd3: return 0xa4e2; // 'も'
		case 0xd4: return 0xa4e4; // 'や'
		case 0xd5: return 0xa4e6; // 'ゆ'
		case 0xd6: return 0xa4e8; // 'よ'
		case 0xd7: return 0xa4e9; // 'ら'
		case 0xd8: return 0xa4ea; // 'り'
		case 0xd9: return 0xa4eb; // 'る'
		case 0xda: return 0xa4ec; // 'れ'
		case 0xdb: return 0xa4ed; // 'ろ'
		case 0xdc: return 0xa4ef; // 'わ'
		case 0xdd: return 0xa4f3; // 'ん'
		case 0xde: return 0xa961; // '゛'
		case 0xdf: return 0xa962; // '゜'
	}
	return code;
}

uint16 AGS::convert_hankaku(uint16 code)
{
	switch(code) {
		case 0x8140: return 0x20; // '　'
		case 0x8149: return 0x21; // '！'
		case 0x8168: return 0x22; // '”'
		case 0x8194: return 0x23; // '＃'
		case 0x8190: return 0x24; // '＄'
		case 0x8193: return 0x25; // '％'
		case 0x8195: return 0x26; // '＆'
		case 0x8166: return 0x27; // '’'
		case 0x8169: return 0x28; // '（'
		case 0x816a: return 0x29; // '）'
		case 0x8196: return 0x2a; // '＊'
		case 0x817b: return 0x2b; // '＋'
		case 0x8143: return 0x2c; // '，'
		case 0x817c: return 0x2d; // '−'
		case 0x8144: return 0x2e; // '．'
		case 0x815e: return 0x2f; // '／'
		case 0x824f: return 0x30; // '０'
		case 0x8250: return 0x31; // '１'
		case 0x8251: return 0x32; // '２'
		case 0x8252: return 0x33; // '３'
		case 0x8253: return 0x34; // '４'
		case 0x8254: return 0x35; // '５'
		case 0x8255: return 0x36; // '６'
		case 0x8256: return 0x37; // '７'
		case 0x8257: return 0x38; // '８'
		case 0x8258: return 0x39; // '９'
		case 0x8146: return 0x3a; // '：'
		case 0x8147: return 0x3b; // '；'
		case 0x8183: return 0x3c; // '＜'
		case 0x8181: return 0x3d; // '＝'
		case 0x8184: return 0x3e; // '＞'
		case 0x8148: return 0x3f; // '？'
		case 0x8197: return 0x40; // '＠'
		case 0x8260: return 0x41; // 'Ａ'
		case 0x8261: return 0x42; // 'Ｂ'
		case 0x8262: return 0x43; // 'Ｃ'
		case 0x8263: return 0x44; // 'Ｄ'
		case 0x8264: return 0x45; // 'Ｅ'
		case 0x8265: return 0x46; // 'Ｆ'
		case 0x8266: return 0x47; // 'Ｇ'
		case 0x8267: return 0x48; // 'Ｈ'
		case 0x8268: return 0x49; // 'Ｉ'
		case 0x8269: return 0x4a; // 'Ｊ'
		case 0x826a: return 0x4b; // 'Ｋ'
		case 0x826b: return 0x4c; // 'Ｌ'
		case 0x826c: return 0x4d; // 'Ｍ'
		case 0x826d: return 0x4e; // 'Ｎ'
		case 0x826e: return 0x4f; // 'Ｏ'
		case 0x826f: return 0x50; // 'Ｐ'
		case 0x8270: return 0x51; // 'Ｑ'
		case 0x8271: return 0x52; // 'Ｒ'
		case 0x8272: return 0x53; // 'Ｓ'
		case 0x8273: return 0x54; // 'Ｔ'
		case 0x8274: return 0x55; // 'Ｕ'
		case 0x8275: return 0x56; // 'Ｖ'
		case 0x8276: return 0x57; // 'Ｗ'
		case 0x8277: return 0x58; // 'Ｘ'
		case 0x8278: return 0x59; // 'Ｙ'
		case 0x8279: return 0x5a; // 'Ｚ'
		case 0x816d: return 0x5b; // '［'
		case 0x818f: return 0x5c; // '¥'
		case 0x816e: return 0x5d; // '］'
		case 0x814f: return 0x5e; // '＾'
		case 0x8151: return 0x5f; // '＿'
		case 0x8165: return 0x60; // '‘'
		case 0x8281: return 0x61; // 'ａ'
		case 0x8282: return 0x62; // 'ｂ'
		case 0x8283: return 0x63; // 'ｃ'
		case 0x8284: return 0x64; // 'ｄ'
		case 0x8285: return 0x65; // 'ｅ'
		case 0x8286: return 0x66; // 'ｆ'
		case 0x8287: return 0x67; // 'ｇ'
		case 0x8288: return 0x68; // 'ｈ'
		case 0x8289: return 0x69; // 'ｉ'
		case 0x828a: return 0x6a; // 'ｊ'
		case 0x828b: return 0x6b; // 'ｋ'
		case 0x828c: return 0x6c; // 'ｌ'
		case 0x828d: return 0x6d; // 'ｍ'
		case 0x828e: return 0x6e; // 'ｎ'
		case 0x828f: return 0x6f; // 'ｏ'
		case 0x8290: return 0x70; // 'ｐ'
		case 0x8291: return 0x71; // 'ｑ'
		case 0x8292: return 0x72; // 'ｒ'
		case 0x8293: return 0x73; // 'ｓ'
		case 0x8294: return 0x74; // 'ｔ'
		case 0x8295: return 0x75; // 'ｕ'
		case 0x8296: return 0x76; // 'ｖ'
		case 0x8297: return 0x77; // 'ｗ'
		case 0x8298: return 0x78; // 'ｘ'
		case 0x8299: return 0x79; // 'ｙ'
		case 0x829a: return 0x7a; // 'ｚ'
		case 0x816f: return 0x7b; // '｛'
		case 0x8162: return 0x7c; // '｜'
		case 0x8170: return 0x7d; // '｝'
		case 0x8160: return 0x7e; // '〜'
		case 0x8142: return 0xa1; // '。'
		case 0x8175: return 0xa2; // '「'
		case 0x8176: return 0xa3; // '」'
		case 0x8141: return 0xa4; // '、'
		case 0x8145: return 0xa5; // '・'
		case 0x82f0: return 0xa6; // 'を'
		case 0x829f: return 0xa7; // 'ぁ'
		case 0x82a1: return 0xa8; // 'ぃ'
		case 0x82a3: return 0xa9; // 'ぅ'
		case 0x82a5: return 0xaa; // 'ぇ'
		case 0x82a7: return 0xab; // 'ぉ'
		case 0x82e1: return 0xac; // 'ゃ'
		case 0x82e3: return 0xad; // 'ゅ'
		case 0x82e5: return 0xae; // 'ょ'
		case 0x82c1: return 0xaf; // 'っ'
		case 0x815b: return 0xb0; // 'ー'
		case 0x82a0: return 0xb1; // 'あ'
		case 0x82a2: return 0xb2; // 'い'
		case 0x82a4: return 0xb3; // 'う'
		case 0x82a6: return 0xb4; // 'え'
		case 0x82a8: return 0xb5; // 'お'
		case 0x82a9: return 0xb6; // 'か'
		case 0x82ab: return 0xb7; // 'き'
		case 0x82ad: return 0xb8; // 'く'
		case 0x82af: return 0xb9; // 'け'
		case 0x82b1: return 0xba; // 'こ'
		case 0x82b3: return 0xbb; // 'さ'
		case 0x82b5: return 0xbc; // 'し'
		case 0x82b7: return 0xbd; // 'す'
		case 0x82b9: return 0xbe; // 'せ'
		case 0x82bb: return 0xbf; // 'そ'
		case 0x82bd: return 0xc0; // 'た'
		case 0x82bf: return 0xc1; // 'ち'
		case 0x82c2: return 0xc2; // 'つ'
		case 0x82c4: return 0xc3; // 'て'
		case 0x82c6: return 0xc4; // 'と'
		case 0x82c8: return 0xc5; // 'な'
		case 0x82c9: return 0xc6; // 'に'
		case 0x82ca: return 0xc7; // 'ぬ'
		case 0x82cb: return 0xc8; // 'ね'
		case 0x82cc: return 0xc9; // 'の'
		case 0x82cd: return 0xca; // 'は'
		case 0x82d0: return 0xcb; // 'ひ'
		case 0x82d3: return 0xcc; // 'ふ'
		case 0x82d6: return 0xcd; // 'へ'
		case 0x82d9: return 0xce; // 'ほ'
		case 0x82dc: return 0xcf; // 'ま'
		case 0x82dd: return 0xd0; // 'み'
		case 0x82de: return 0xd1; // 'む'
		case 0x82df: return 0xd2; // 'め'
		case 0x82e0: return 0xd3; // 'も'
		case 0x82e2: return 0xd4; // 'や'
		case 0x82e4: return 0xd5; // 'ゆ'
		case 0x82e6: return 0xd6; // 'よ'
		case 0x82e7: return 0xd7; // 'ら'
		case 0x82e8: return 0xd8; // 'り'
		case 0x82e9: return 0xd9; // 'る'
		case 0x82ea: return 0xda; // 'れ'
		case 0x82eb: return 0xdb; // 'ろ'
		case 0x82ed: return 0xdc; // 'わ'
		case 0x82f1: return 0xdd; // 'ん'
		case 0x814a: return 0xde; // '゛'
		case 0x814b: return 0xdf; // '゜'
	}
	return code;
}

extern "C" {

void EMSCRIPTEN_KEEPALIVE ags_setAntialiasedStringMode(int on) {
	antialias = on != 0;
}

}
