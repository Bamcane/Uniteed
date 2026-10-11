#ifndef ENGINE_KEYCODES_H
#define ENGINE_KEYCODES_H

/*
	The key symbols the editor asks about, with the values SDL3 uses so the
	window build can hand an SDL event straight to CInput without a mapping
	table.

	They live here rather than behind <SDL3/SDL_keycode.h> because the editor
	model is also driven without a window: the command line, the scripted
	screenshot mode and the agent interface all push the same key codes into
	CInput, and the headless build must not depend on SDL at all.
*/

namespace engine::keys
{
	inline constexpr int Escape = 0x0000001b;
	inline constexpr int Return = 0x0000000d;
	inline constexpr int KpEnter = 0x40000058;
	inline constexpr int Backspace = 0x00000008;
	inline constexpr int Tab = 0x00000009;
	inline constexpr int Space = 0x00000020;
	inline constexpr int Delete = 0x0000007f;
	inline constexpr int Minus = 0x0000002d;
	inline constexpr int Equals = 0x0000003d;
	inline constexpr int KpMinus = 0x40000056;
	inline constexpr int KpPlus = 0x40000057;
	inline constexpr int LeftBracket = 0x0000005b;
	inline constexpr int RightBracket = 0x0000005d;
	// the pair the tile index is cycled with, next to the other two so a hand
	// on the home row does not have to move
	inline constexpr int Comma = 0x0000002c;
	inline constexpr int Period = 0x0000002e;
	// "?" is Shift+/ on most layouts, but the key event carries the shifted
	// character, so both spellings are bound to the same action
	inline constexpr int Slash = 0x0000002f;
	inline constexpr int Question = 0x0000003f;
	inline constexpr int Home = 0x4000004a;
	inline constexpr int End = 0x4000004d;
	inline constexpr int PageUp = 0x4000004b;
	inline constexpr int PageDown = 0x4000004e;

	inline constexpr int Left = 0x40000050;
	inline constexpr int Right = 0x4000004f;
	inline constexpr int Up = 0x40000052;
	inline constexpr int Down = 0x40000051;

	inline constexpr int LCtrl = 0x400000e0;
	inline constexpr int RCtrl = 0x400000e4;
	inline constexpr int LShift = 0x400000e1;
	inline constexpr int RShift = 0x400000e5;
	inline constexpr int LAlt = 0x400000e2;
	inline constexpr int RAlt = 0x400000e6;

	inline constexpr int A = 0x00000061;
	inline constexpr int B = 0x00000062;
	inline constexpr int C = 0x00000063;
	inline constexpr int D = 0x00000064;
	inline constexpr int E = 0x00000065;
	inline constexpr int F = 0x00000066;
	inline constexpr int G = 0x00000067;
	inline constexpr int H = 0x00000068;
	inline constexpr int I = 0x00000069;
	inline constexpr int J = 0x0000006a;
	inline constexpr int K = 0x0000006b;
	inline constexpr int L = 0x0000006c;
	inline constexpr int M = 0x0000006d;
	inline constexpr int N = 0x0000006e;
	inline constexpr int O = 0x0000006f;
	inline constexpr int P = 0x00000070;
	inline constexpr int Q = 0x00000071;
	inline constexpr int R = 0x00000072;
	inline constexpr int S = 0x00000073;
	inline constexpr int T = 0x00000074;
	inline constexpr int U = 0x00000075;
	inline constexpr int V = 0x00000076;
	inline constexpr int W = 0x00000077;
	inline constexpr int X = 0x00000078;
	inline constexpr int Y = 0x00000079;
	inline constexpr int Z = 0x0000007a;

	inline constexpr int Digit0 = 0x00000030;
	inline constexpr int Digit1 = 0x00000031;
	inline constexpr int Digit2 = 0x00000032;
	inline constexpr int Digit3 = 0x00000033;
	inline constexpr int Digit4 = 0x00000034;
	inline constexpr int Digit5 = 0x00000035;
	inline constexpr int Digit6 = 0x00000036;
	inline constexpr int Digit7 = 0x00000037;
	inline constexpr int Digit8 = 0x00000038;
	inline constexpr int Digit9 = 0x00000039;
} // namespace engine::keys

#endif // ENGINE_KEYCODES_H
